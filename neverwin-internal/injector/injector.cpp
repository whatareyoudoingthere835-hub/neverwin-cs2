// Простой x64-инжектор для neverwin.dll.
// Каждый шаг печатает человекочитаемую ошибку — это главный
// диагностический инструмент для "вылетает при инжекте".
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

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
            CloseHandle(hProcess);
            return false;
        }

        const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (!loadLibrary) {
            PrintLastError(L"GetProcAddress(LoadLibraryW)");
            CloseHandle(hProcess);
            return false;
        }

        const HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, loadLibrary, remote, 0, nullptr);
        if (!hThread) {
            PrintLastError(L"CreateRemoteThread");
            CloseHandle(hProcess);
            return false;
        }

        WaitForSingleObject(hThread, 10000);
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);
        // exitCode == 0 => LoadLibraryW вернул NULL => DLL не загрузилась.
        wprintf(L"[%s] LoadLibraryW вернул 0x%08lX\n", exitCode ? L"+" : L"-", exitCode);
        if (!exitCode) {
            wprintf(L"    -> Причины: DLL не x64 / собрана в Debug / не найдена зависимость.\n"
                    L"    -> Детали ищи в %%TEMP%%\\neverwin.log — DLL пишет туда свой лог.\n");
        }

        CloseHandle(hThread);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return exitCode != 0;
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

    const std::wstring dllPath = argc > 2 ? argv[2] : L"neverwin.dll";
    wprintf(L"[*] Цель: cs2.exe (PID %lu)\n", pid);

    const bool ok = Inject(pid, dllPath);
    wprintf(ok ? L"[+] Готово. В игре: INSERT — меню, END — выгрузка.\n"
               : L"[-] Инжект не удался, см. ошибки выше.\n");
    return ok ? 0 : 1;
}
