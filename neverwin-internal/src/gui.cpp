#include "pch.h"
#include "gui.hpp"
#include "features.hpp"
#include "log.hpp"
#include "util.hpp"

#include <dxgi1_2.h> // IDXGIFactory2/IDXGISwapChain1/DXGI_SWAP_CHAIN_DESC1 (mingw: отдельно от dxgi.h)

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// Внутренние функции бэкенда DX11, нужные для обработки ресайза окна.
// В официальном заголовке они не объявлены, но и не static — объявляем сами.
bool ImGui_ImplDX11_CreateDeviceObjects();
void ImGui_ImplDX11_InvalidateDeviceObjects();

// В imgui 1.93 WIP декларация WndProcHandler убрана из заголовка в #if 0
// (чтобы не тащить windows.h в хедер) — объявляем сами, функция не static.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace gui {
    std::atomic<bool> g_menuOpen{false};
    std::atomic<bool> g_unloadRequested{false};
    std::atomic<bool> g_hudVisible{true};
}

namespace {

    // Типы хукаемых функций.
    using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using ResizeFn  = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    // Индексы в vtable IDXGISwapChain: 7 методов IDXGIObject + GetDevice,
    // дальше первым собственным методом идёт Present (индекс 8),
    // шестым собственным — ResizeBuffers (индекс 13).
    constexpr int kPresentIndex       = 8;
    constexpr int kResizeBuffersIndex = 13;

    // IID объявлены явно, чтобы не зависеть от dxguid.lib / uuid.lib.
    const GUID kIID_ID3D11Device = {
        0xdb6f6ddb, 0xac77, 0x4e88, { 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40 } };
    const GUID kIID_IDXGIFactory2 = {
        0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };

    // Состояние хуков. CS2 презентит через IDXGISwapChain1, у которой СВОЯ
    // vtable — поэтому хуков два, и у каждого свои оригиналы.
    void**    g_vtable           = nullptr;  // vtable IDXGISwapChain, которую пропатчили
    void**    g_vtable1          = nullptr;  // vtable IDXGISwapChain1
    PresentFn g_originalPresent  = nullptr;
    ResizeFn  g_originalResize   = nullptr;
    PresentFn g_originalPresent1 = nullptr;
    ResizeFn  g_originalResize1  = nullptr;
    WNDPROC   g_originalWndProc = nullptr;
    HWND      g_gameWindow      = nullptr;
    HANDLE    g_eventUnhooked   = nullptr;  // "все хуки сняты, можно free"
    std::atomic<bool> g_imguiReady{false};
    std::atomic<bool> g_wndprocRestored{false};

    // Форвард-декларации: нужны для InitImGui и HookSwapchain.
    LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
        IDXGISwapChain* swapChain, UINT bufferCount, UINT width, UINT height,
        DXGI_FORMAT newFormat, UINT swapChainFlags);

    // --- Поиск главного видимого окна нашего процесса. ---
    HWND FindGameWindow() {
        struct EnumCtx { DWORD pid; HWND found; };
        EnumCtx ctx{ GetCurrentProcessId(), nullptr };

        EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
            EnumCtx* c = reinterpret_cast<EnumCtx*>(lparam);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != c->pid)
                return TRUE;
            if (GetWindow(hwnd, GW_OWNER) != nullptr)   // не топ-левел
                return TRUE;
            if (!(GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE))
                return TRUE;
            wchar_t title[128]{};
            GetWindowTextW(hwnd, title, 127);
            if (title[0] == L'\0')
                return TRUE;
            c->found = hwnd;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&ctx));

        return ctx.found;
    }

    // --- Снятие хуков (идемпотентно). ---
    void RestoreVtable() {
        const auto restore = [](void** vtable, void* present, void* resize) {
            if (!vtable)
                return;
            DWORD old = 0;
            VirtualProtect(vtable, sizeof(void*) * 24, PAGE_READWRITE, &old);
            if (present)
                vtable[kPresentIndex] = present;
            if (resize)
                vtable[kResizeBuffersIndex] = resize;
            VirtualProtect(vtable, sizeof(void*) * 24, old, &old);
        };
        restore(g_vtable,  reinterpret_cast<void*>(g_originalPresent),  reinterpret_cast<void*>(g_originalResize));
        restore(g_vtable1, reinterpret_cast<void*>(g_originalPresent1), reinterpret_cast<void*>(g_originalResize1));
        g_vtable  = nullptr;
        g_vtable1 = nullptr;
    }

    // Оригинальный Present для конкретного swapchain: у IDXGISwapChain и
    // IDXGISwapChain1 разные vtable и, возможно, разные функции.
    PresentFn OriginalPresentFor(IDXGISwapChain* swapChain) {
        if (g_vtable1) {
            void** vt = *reinterpret_cast<void***>(swapChain);
            if (vt == g_vtable1 && g_originalPresent1)
                return g_originalPresent1;
        }
        return g_originalPresent;
    }

    ResizeFn OriginalResizeFor(IDXGISwapChain* swapChain) {
        if (g_vtable1) {
            void** vt = *reinterpret_cast<void***>(swapChain);
            if (vt == g_vtable1 && g_originalResize1)
                return g_originalResize1;
        }
        return g_originalResize;
    }

    void RestoreWndProc() {
        // Указатель g_originalWndProc НЕ зануляем: HookedWndProc может
        // вызвать CallWindowProcW уже после снятия хука — ему нужен оригинал.
        if (g_wndprocRestored.load() || !g_gameWindow || !g_originalWndProc)
            return;
        SetWindowLongPtrW(g_gameWindow, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(g_originalWndProc));
        g_wndprocRestored.store(true);
    }

    void ShutdownImGui() {
        if (!g_imguiReady.load())
            return;
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiReady.store(false);
    }

    // --- Шрифт с кириллицей: встроенный шрифт ImGui русский не рисует. ---
    void SetupFonts() {
        ImGuiIO& io = ImGui::GetIO();
        const wchar_t* candidates[] = {
            L"C:\\Windows\\Fonts\\segoeui.ttf",
            L"C:\\Windows\\Fonts\\arial.ttf",
        };
        ImFontConfig cfg{};
        cfg.PixelSnapH = true;

        for (const wchar_t* path : candidates) {
            if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
                continue;
            const std::string utf8 = util::ToUtf8(path);
            io.Fonts->AddFontFromFileTTF(utf8.c_str(), 15.0f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
            NW_LOG(L"шрифт меню: %s", path);
            return;
        }
        NW_LOG(L"WARNING: системный шрифт не найден — меню будет с квадратами.");
    }

    // --- Ленивая инициализация ImGui на первом Present игры. ---
    bool InitImGui(IDXGISwapChain* swapChain) {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;

        if (FAILED(swapChain->GetDevice(kIID_ID3D11Device, reinterpret_cast<void**>(&device)))) {
            NW_LOG(L"GetDevice(IID_ID3D11Device) провалился");
            return false;
        }
        device->GetImmediateContext(&context);

        const HWND hwnd = FindGameWindow();
        if (!hwnd) {
            NW_LOG(L"не нашли окно игры");
            device->Release();
            context->Release();
            return false;
        }
        g_gameWindow = hwnd;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        SetupFonts();
        ImGui::StyleColorsDark();
        ImGui::GetIO().IniFilename = nullptr; // не мусорить imgui.ini в папке игры

        if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(device, context)) {
            NW_LOG(L"инициализация ImGui провалилась");
            ImGui::DestroyContext();
            device->Release();
            context->Release();
            return false;
        }

        // Бэкенд хранит сырые указатели и не делает AddRef —
        // свои ссылки отпускаем.
        context->Release();
        device->Release();

        g_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
        if (!g_originalWndProc) {
            NW_LOG(L"не удалось повесить WndProc");
            ShutdownImGui();
            return false;
        }

        g_imguiReady.store(true);
        NW_LOG(L"ImGui готов: окно 0x%llX",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd)));
        return true;
    }

    // --- Хук WndProc: кормим ImGui ввод, при выгрузке снимаемся сами. ---
    LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (gui::g_unloadRequested.load()) {
            RestoreWndProc();
        }
        if (g_imguiReady.load()) {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
        }
        return CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam);
    }

    // --- Меню. ---
    void RenderMenu() {
        if (!gui::g_menuOpen.load())
            return;

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("NEVERWIN", &open, ImGuiWindowFlags_NoCollapse);
        if (!open)
            gui::g_menuOpen.store(false);

        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
                           "professional software for losing professionally");
        ImGui::Separator();

        bool v;

        v = g_features.antiAimbot.load();
        if (ImGui::Checkbox("Реверс аимбот: наводка на тимейтов [F1]", &v))
            g_features.antiAimbot.store(v);

        v = g_features.antiAimless.load();
        if (ImGui::Checkbox("Антиаимлесс: взгляд в пол [F2]", &v))
            g_features.antiAimless.store(v);

        v = g_features.visualRecoil.load();
        if (ImGui::Checkbox("Visual recoil x4 [F3]", &v))
            g_features.visualRecoil.store(v);

        v = g_features.antiBhop.load();
        if (ImGui::Checkbox("Антибхоп [F4]", &v))
            g_features.antiBhop.store(v);

        v = g_features.gamesense.load();
        if (ImGui::Checkbox("Gamesense: дроп оружия [F5]", &v))
            g_features.gamesense.store(v);

        ImGui::Separator();

        const uintptr_t base = g_state.clientBase.load();
        ImGui::Text("client.dll:  0x%llX", static_cast<unsigned long long>(base));
        ImGui::Text("LocalPlayer: 0x%llX  (hp %d, team %d)",
                    static_cast<unsigned long long>(g_state.localPlayer.load()),
                    g_state.localHealth.load(),
                    g_state.localTeam.load());
        ImGui::Text("EntityList:  0x%llX",
                    static_cast<unsigned long long>(g_state.entityList.load()));

        ImGui::TextColored(
            g_state.offsetsFromIni.load() ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f) : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
            "%s",
            g_state.offsetsFromIni.load()
                ? "Оффсеты: из neverwin.ini"
                : "Оффсеты: ВСТРОЕННЫЕ — Valve обновили игру? Сгенерируй ini!");

        ImGui::Separator();

        if (ImGui::Button("Выгрузить DLL", ImVec2(-1, 0))) {
            gui::g_unloadRequested.store(true);
        }
        ImGui::TextDisabled("v%d | INSERT - меню | F6 - HUD | END - выгрузка", NW_VERSION);

        ImGui::End();
    }

    // --- Хук Present: рисуем меню перед отдачей кадра игре. ---
    HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
        // Оригинал вычисляем ДО RestoreVtable: после снятия хука vtable-указатель
        // уже не сопоставить с сохранёнными.
        const PresentFn orig = OriginalPresentFor(swapChain);

        if (gui::g_unloadRequested.load()) {
            // Порядок важен: сначала снимаем WndProc, потом убиваем ImGui,
            // потом восстанавливаем vtable. Событие — в самом конце, чтобы
            // поток фич не освободил DLL, пока мы ещё в этой функции.
            RestoreWndProc();
            ShutdownImGui();
            RestoreVtable();
            const HRESULT hr = orig(swapChain, syncInterval, flags);
            SetEvent(g_eventUnhooked);
            return hr;
        }

        if (!g_imguiReady.load() && !InitImGui(swapChain)) {
            return orig(swapChain, syncInterval, flags);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        RenderMenu();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        return orig(swapChain, syncInterval, flags);
    }

    // --- Хук ResizeBuffers: пересоздаём объекты бэкенда после ресайза. ---
    HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
        IDXGISwapChain* swapChain, UINT bufferCount, UINT width, UINT height,
        DXGI_FORMAT newFormat, UINT swapChainFlags) {
        const ResizeFn orig = OriginalResizeFor(swapChain);

        if (g_imguiReady.load())
            ImGui_ImplDX11_InvalidateDeviceObjects();

        const HRESULT hr = orig(swapChain, bufferCount, width, height, newFormat, swapChainFlags);

        if (g_imguiReady.load())
            (void)ImGui_ImplDX11_CreateDeviceObjects();

        return hr;
    }

    // --- Установка хука через dummy-устройство. ---
    bool HookSwapchain() {
        // Dummy-окно делаем своё: если вешать второй swapchain на окно игры,
        // в exclusive fullscreen DXGI может отказать (DXGI_ERROR_INVALID_CALL).
        const wchar_t kDummyClass[] = L"NeverwinDummyWnd";
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDummyClass;
        RegisterClassExW(&wc); // если уже зарегистрирован — вернёт 0, не страшно

        HWND dummyWindow = CreateWindowExW(
            0, kDummyClass, L"", WS_OVERLAPPEDWINDOW,
            0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
        if (!dummyWindow)
            dummyWindow = GetDesktopWindow(); // крайний случай — хоть какое-то окно

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate  = { 60, 1 };
        sd.SampleDesc.Count        = 1;
        sd.BufferUsage             = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount             = 1;
        sd.OutputWindow            = dummyWindow;
        sd.Windowed                = TRUE;

        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        IDXGISwapChain* dummySwapChain = nullptr;
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

        const HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &dummySwapChain, &device, &level, &context);

        if (FAILED(hr) || !dummySwapChain) {
            if (dummyWindow && dummyWindow != GetDesktopWindow())
                DestroyWindow(dummyWindow);
            UnregisterClassW(kDummyClass, wc.hInstance);
            NW_LOG(L"D3D11CreateDeviceAndSwapChain: 0x%08X", static_cast<unsigned>(hr));
            return false;
        }

        // 1) Хук IDXGISwapChain (классика). CS2 этим классом не презентит,
        //    но пусть будет — вдруг другой рендер-путь.
        g_vtable = *reinterpret_cast<void***>(dummySwapChain);

        DWORD old = 0;
        VirtualProtect(g_vtable, sizeof(void*) * 24, PAGE_READWRITE, &old);
        g_originalPresent = reinterpret_cast<PresentFn>(g_vtable[kPresentIndex]);
        g_originalResize  = reinterpret_cast<ResizeFn>(g_vtable[kResizeBuffersIndex]);
        g_vtable[kPresentIndex]       = reinterpret_cast<void*>(&HookedPresent);
        g_vtable[kResizeBuffersIndex] = reinterpret_cast<void*>(&HookedResizeBuffers);
        VirtualProtect(g_vtable, sizeof(void*) * 24, old, &old);

        // 2) Хук IDXGISwapChain1: CS2 создаёт swapchain через
        //    CreateSwapChainForHwnd, и это ДРУГОЙ класс со своей vtable.
        //    Без этого хука меню на DX11 не появлялось вообще.
        {
            IDXGIFactory2* factory2 = nullptr;
            const HRESULT hrFactory = CreateDXGIFactory1(
                kIID_IDXGIFactory2, reinterpret_cast<void**>(&factory2));
            if (SUCCEEDED(hrFactory) && factory2) {
                DXGI_SWAP_CHAIN_DESC1 sd1{};
                sd1.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
                sd1.SampleDesc.Count = 1;
                sd1.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                sd1.BufferCount      = 1;

                IDXGISwapChain1* sc1 = nullptr;
                const HRESULT hr1 = factory2->CreateSwapChainForHwnd(
                    device, dummyWindow, &sd1, nullptr, nullptr, &sc1);
                if (SUCCEEDED(hr1) && sc1) {
                    g_vtable1 = *reinterpret_cast<void***>(sc1);
                    if (g_vtable1 != g_vtable) {
                        DWORD old1 = 0;
                        VirtualProtect(g_vtable1, sizeof(void*) * 24, PAGE_READWRITE, &old1);
                        g_originalPresent1 = reinterpret_cast<PresentFn>(g_vtable1[kPresentIndex]);
                        g_originalResize1  = reinterpret_cast<ResizeFn>(g_vtable1[kResizeBuffersIndex]);
                        g_vtable1[kPresentIndex]       = reinterpret_cast<void*>(&HookedPresent);
                        g_vtable1[kResizeBuffersIndex] = reinterpret_cast<void*>(&HookedResizeBuffers);
                        VirtualProtect(g_vtable1, sizeof(void*) * 24, old1, &old1);
                        NW_LOG(L"хук Present установлен и на IDXGISwapChain1 (vtable 0x%llX)",
                               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_vtable1)));
                    } else {
                        g_vtable1 = nullptr; // та же vtable — уже пропатчена
                    }
                    sc1->Release();
                } else {
                    NW_LOG(L"CreateSwapChainForHwnd: 0x%08X — только хук IDXGISwapChain",
                           static_cast<unsigned>(hr1));
                }
                factory2->Release();
            }
        }

        // Dummy-цепочки и окно больше не нужны — vtable пропатчены.
        if (dummyWindow && dummyWindow != GetDesktopWindow())
            DestroyWindow(dummyWindow);
        UnregisterClassW(kDummyClass, wc.hInstance);
        dummySwapChain->Release();
        device->Release();
        context->Release();

        NW_LOG(L"хуки Present установлены (IDXGISwapChain 0x%llX, IDXGISwapChain1 0x%llX)",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_vtable)),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_vtable1)));
        return true;
    }
}

namespace gui {

    bool Init() {
        g_eventUnhooked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!HookSwapchain()) {
            NW_LOG(L"WARNING: хук Present не встал — меню будет недоступно (фичи работают).");
        }
        return g_vtable != nullptr || g_vtable1 != nullptr;
    }

    [[noreturn]] void ShutdownAndExit(HMODULE hModule) {
        g_unloadRequested.store(true);
        NW_LOG(L"выгрузка: жду, пока рендер-поток снимет хуки...");

        if (g_eventUnhooked)
            WaitForSingleObject(g_eventUnhooked, 3000);

        // Небольшой запас: дать рендер-потоку гарантированно выйти из нашего кода.
        Sleep(200);

        NW_LOG(L"выгружаю DLL.");
        FreeLibraryAndExitThread(hModule, 0);
    }
}
