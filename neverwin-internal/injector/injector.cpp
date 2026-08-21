// Простой x64-инжектор для neverwin.dll.
//
// После CreateRemoteThread инжектор проверяет ФАКТИЧЕСКОЕ наличие модуля
// в процессе цели через снапшот модулей (Module32First/Next) — это
// единственный честный признак успеха. Раньше успех определялся по
// выходному коду потока LoadLibraryW, и он врал в двух случаях:
//   * поток не завершился за 10 сек -> GetExitCodeThread даёт STILL_ACTIVE
//     (259), ненулевой -> выглядело как успех;
//   * DLL загрузилась и сразу выгрузилась сама (например, не дождалась
//     client.dll) -> LoadLibraryW вернул валидный адрес -> выглядело как
//     успех, хотя модуля уже нет.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <string>

// DLL по умолчанию (если запущен без аргументов). Для velocity-релиза
// собирается с -DINJECTOR_DEFAULT_DLL="velocity_v1.dll".
#ifndef INJECTOR_DEFAULT_DLL
#define INJECTOR_DEFAULT_DLL "neverwin.dll"
#endif

namespace {

    void PrintLastError(const wchar_t* step) {
        const DWORD err = GetLastError();
        wchar_t* msg = nullptr;
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
        wprintf(L"[!] %s не удалось (код %lu): %s\n", step, err, msg ? msg : L"(нет описания)");
        if (msg)
            LocalFree(msg);

        // Частые случаи — сразу с объяснением.
        if (err == ERROR_BAD_EXE_FORMAT) {
            wprintf(L"    -> Разрядность не совпадает: и инжектор, и DLL должны быть x64, а цель — cs2.exe (x64).\n");
        } else if (err == ERROR_MOD_NOT_FOUND || err == ERROR_FILE_NOT_FOUND) {
            wprintf(L"    -> DLL или её зависимости не найдены. Собирал Debug? Нужен Release:\n"
                    L"       на целевой машине нет Debug-рантайма (vcruntime140d.dll и т.п.).\n");
        } else if (err == ERROR_ACCESS_DENIED) {
            wprintf(L"    -> Нет прав: запусти инжектор от администратора или проверь антивирус.\n");
        }
    }

    DWORD FindProcessId(const wchar_t* name) {
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;

        DWORD pid = 0;
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, name) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return pid;
    }

    bool Is64BitProcess(DWORD pid) {
        const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h)
            return false;
        BOOL wow64 = FALSE;
        const BOOL ok = IsWow64Process(h, &wow64);
        CloseHandle(h);
        return ok && !wow64;
    }

    // ЗАГРУЖЕН ЛИ МОДУЛЬ В ПРОЦЕССЕ — вот это и есть "заинжектилось".
    bool IsModuleLoaded(DWORD pid, const wchar_t* moduleName) {
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE)
            return false;

        MODULEENTRY32W me{ sizeof(me) };
        bool found = false;
        if (Module32FirstW(snap, &me)) {
            do {
                if (_wcsicmp(me.szModule, moduleName) == 0) {
                    found = true;
                    break;
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        return found;
    }

    bool Inject(DWORD pid, const std::wstring& dllPath) {
        // Полный путь обязателен: LoadLibraryW ищет относительно процесса-цели.
        wchar_t fullPath[MAX_PATH]{};
        if (!GetFullPathNameW(dllPath.c_str(), MAX_PATH, fullPath, nullptr)) {
            PrintLastError(L"GetFullPathNameW");
            return false;
        }
        if (GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) {
            wprintf(L"[!] DLL не найдена: %s\n", fullPath);
            return false;
        }
        wprintf(L"[+] DLL: %s\n", fullPath);

        // Имя файла — для проверки в снапшоте модулей.
        const wchar_t* baseName = wcsrchr(fullPath, L'\\');
        baseName = baseName ? baseName + 1 : fullPath;

        const HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess) {
            PrintLastError(L"OpenProcess");
            return false;
        }

        const size_t pathBytes = (wcslen(fullPath) + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(hProcess, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote) {
            PrintLastError(L"VirtualAllocEx");
            CloseHandle(hProcess);
            return false;
        }

        if (!WriteProcessMemory(hProcess, remote, fullPath, pathBytes, nullptr)) {
            PrintLastError(L"WriteProcessMemory");
            VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }

        const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (!loadLibrary) {
            PrintLastError(L"GetProcAddress(LoadLibraryW)");
            VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }

        const HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (!hThread) {
            PrintLastError(L"CreateRemoteThread");
            VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
            CloseHandle(hProcess);
            return false;
        }

        const DWORD wait = WaitForSingleObject(hThread, 10000);
        DWORD exitCode = 0;
        if (!GetExitCodeThread(hThread, &exitCode)) {
            PrintLastError(L"GetExitCodeThread");
        }

        if (wait == WAIT_TIMEOUT || exitCode == STILL_ACTIVE) {
            wprintf(L"[!] Поток LoadLibraryW не завершился за 10 сек (код 0x%08lX).\n", exitCode);
            wprintf(L"    -> Похоже на дедлок loader lock или зависание в DllMain.\n");
        } else if (wait == WAIT_FAILED) {
            PrintLastError(L"WaitForSingleObject");
        } else {
            wprintf(L"[%s] LoadLibraryW вернул 0x%08lX\n", exitCode ? L"+" : L"-", exitCode);
            if (!exitCode) {
                wprintf(L"    -> LoadLibraryW вернул NULL: DLL не x64 / собрана в Debug /\n"
                        L"       не найдена зависимость. Детали — в %%TEMP%%\\neverwin.log.\n");
            }
        }

        // ЧЕСТНАЯ ПРОВЕРКА: модуль реально в процессе?
        // Небольшая пауза: даём DllMain довести работу до конца.
        Sleep(100);
        const bool loaded = IsModuleLoaded(pid, baseName);

        CloseHandle(hThread);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);

        if (loaded) {
            wprintf(L"[+] neverwin.dll в процессе %lu — инжект подтверждён.\n", pid);
            if (wait == WAIT_TIMEOUT || exitCode == STILL_ACTIVE) {
                wprintf(L"    (!) Но поток DllMain так и не завершился — проверь %%TEMP%%\\neverwin.log.\n");
            }
            return true;
        }

        if (exitCode != 0 && exitCode != STILL_ACTIVE && wait != WAIT_TIMEOUT) {
            wprintf(L"[!] LoadLibraryW вернул ненулевой адрес, но модуля в процессе НЕТ.\n"
                    L"    -> DLL загрузилась и сразу выгрузилась сама. Возможные причины:\n"
                    L"       * в процессе не появилась client.dll — DLL ждёт её 120 сек и выходит;\n"
                    L"       * антивирус / VAC выбил модуль из процесса.\n"
                    L"    -> Точную причину пишет сама DLL: открой %%TEMP%%\\neverwin.log.\n");
        } else {
            wprintf(L"[-] Инжект не подтверждён: neverwin.dll в процессе не найден.\n");
        }
        return false;
    }
}

int wmain(int argc, wchar_t* argv[]) {
    wprintf(L"=== neverwin injector (x64) ===\n");

    if (!Is64BitProcess(GetCurrentProcessId())) {
        wprintf(L"[!] Инжектор должен быть собран как x64.\n");
        return 1;
    }

    DWORD pid = 0;
    if (argc > 1)
        pid = static_cast<DWORD>(wcstoul(argv[1], nullptr, 10));

    if (!pid) {
        pid = FindProcessId(L"cs2.exe");
        if (!pid) {
            wprintf(L"[!] cs2.exe не найден.\n"
                    L"    Использование: neverwin_injector.exe [PID] [путь\\к\\neverwin.dll]\n");
            return 1;
        }
    }

    if (!Is64BitProcess(pid)) {
        wprintf(L"[!] Процесс %lu не x64 — инжект невозможен.\n", pid);
        return 1;
    }

    const std::wstring dllPath = argc > 2 ? argv[2] : L"" INJECTOR_DEFAULT_DLL;
    wprintf(L"[*] Цель: cs2.exe (PID %lu)\n", pid);

    const bool ok = Inject(pid, dllPath);
    wprintf(ok ? L"[+] Готово. В игре: INSERT — меню (по умолчанию скрыто), END — выгрузка.\n"
               : L"[-] Инжект не удался, см. ошибки выше.\n");
    return ok ? 0 : 1;
}
