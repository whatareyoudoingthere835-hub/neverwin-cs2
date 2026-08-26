#include "pch.h"
#include "gui.hpp"
#include "features.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "entities.hpp"
#include "offsets.hpp"
#include "assets/gui_icons.hpp"
#include "assets/esp_icons.hpp"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "minhook.h"

// В imgui 1.93 WIP декларация WndProcHandler убрана из заголовка в #if 0 —
// объявляем сами, функция не static.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// МЕНЮ В ИГРЕ — схема quintcs2 (исходник дал passtuh):
//   1. Настоящий свопчейн игры берём НЕ через dummy-свопчейны (это роняло v3),
//      а сигнатурой из rendersystemdx11.dll: глобал -> слот -> c_swap_chain_dx_11*
//      -> +0x170 -> IDXGISwapChain*.
//   2. MinHook на Present (vtable 8) и ResizeBuffers (13) этого свопчейна.
//   3. Каждый кадр: перебиндовываем свой RTV (OMSetRenderTargets) и рисуем
//      ImGui прямо в бэкбуфер игры, потом оригинальный Present.
//   4. InputSystem::IsRelativeMouseMode (vtable 76, InputSystemVersion001 из
//      inputsystem.dll) — когда меню открыто, возвращает false: игра отдаёт
//      курсор, иначе мышь в меню не работает.
//   5. WndProc-хук: тоггл меню, прокидывание ввода в ImGui, глотание клавиш,
//      чтобы игрок не бегал, пока открыто меню.
// Ни dummy-устройств, ни dummy-свопчейнов — меньше точек краша.
// ============================================================================

namespace gui {
    std::atomic<bool> g_menuOpen{false};
    std::atomic<bool> g_unloadRequested{false};
    std::atomic<bool> g_inGameMenuReady{false};
}

namespace {

    using PresentFn  = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using ResizeFn   = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using RelMouseFn = void* (__fastcall*)(void*, bool);

    constexpr int kPresentIdx  = 8;   // IDXGISwapChain::Present
    constexpr int kResizeIdx   = 13;  // IDXGISwapChain::ResizeBuffers
    constexpr int kRelMouseIdx = 76;  // InputSystem::IsRelativeMouseMode (дамп quintcs2)

    HWND                    g_window   = nullptr;
    WNDPROC                 g_origWnd  = nullptr;
    IDXGISwapChain*         g_swapChain = nullptr;
    ID3D11Device*           g_device   = nullptr;
    ID3D11DeviceContext*    g_context  = nullptr;
    ID3D11RenderTargetView* g_rtv      = nullptr;
    PresentFn  g_origPresent  = nullptr;
    ResizeFn   g_origResize   = nullptr;
    RelMouseFn g_origRelMouse = nullptr;
    bool  g_imguiReady    = false;
    ImFont* g_iconFont = nullptr;
    HANDLE g_eventUnhooked = nullptr;

    // --- окно игры (EnumWindows: наше, видимое, без владельца) ---
    HWND FindGameWindow() {
        struct Ctx { DWORD pid; HWND found; } ctx{ GetCurrentProcessId(), nullptr };
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != c->pid)
                return TRUE;
            if (GetWindow(hwnd, GW_OWNER) != nullptr)  // не топ-левел
                return TRUE;
            if (!(GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE))
                return TRUE;
            c->found = hwnd;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&ctx));
        return ctx.found;
    }

    // --- поиск сигнатуры в образе модуля ---
    size_t ModuleSize(HMODULE m) {
        const auto base = reinterpret_cast<const uint8_t*>(m);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;
        return nt->OptionalHeader.SizeOfImage;
    }

    uintptr_t FindPattern(HMODULE m, const uint8_t* bytes, size_t len) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(m);
        const size_t size = ModuleSize(m);
        if (!base || !size)
            return 0;
        for (size_t i = 0; i + len <= size; ++i) {
            if (memcmp(reinterpret_cast<const void*>(base + i), bytes, len) == 0)
                return base + i;
        }
        return 0;
    }

    // --- настоящий свопчейн игры (quintcs2: rendersystemdx11.dll) ---
    // сигнатура: mov [rip+disp], r13 ... movdqu [rip+disp], xmm0
    // глобал -> слот (указатель) -> c_swap_chain_dx_11* -> +0x170 -> IDXGISwapChain*
    bool GrabSwapChain() {
        static const uint8_t kSig[] = {
            0x48, 0x89, 0x2D, 0xE4, 0x21, 0x46, 0x00,
            0x66, 0x0F, 0x7F, 0x05, 0xE4, 0x21, 0x46, 0x00, 0xFF };
        HMODULE rs = GetModuleHandleW(L"rendersystemdx11.dll");
        if (!rs)
            return false;

        const uintptr_t match = FindPattern(rs, kSig, sizeof(kSig));
        if (!match)
            return false;

        const int32_t disp    = mem::Read<int32_t>(match + 3);
        const uintptr_t global = match + 7 + disp;
        const uintptr_t slot   = mem::Read<uintptr_t>(global);   // c_swap_chain_dx_11**
        if (!slot)
            return false;
        const uintptr_t wrapper = mem::Read<uintptr_t>(slot);    // c_swap_chain_dx_11*
        if (!wrapper)
            return false;
        g_swapChain = reinterpret_cast<IDXGISwapChain*>(mem::Read<uintptr_t>(wrapper + 0x170));
        return g_swapChain != nullptr;
    }

    // --- RTV на бэкбуфер игры (пересоздаётся после ресайза) ---
    void DestroyRtv() {
        if (g_rtv) {
            g_rtv->Release();
            g_rtv = nullptr;
        }
    }

    bool EnsureRtv() {
        if (g_rtv)
            return true;
        if (!g_swapChain || !g_device)
            return false;
        ID3D11Texture2D* back = nullptr;
        if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                          reinterpret_cast<void**>(&back))))
            return false;
        const HRESULT hr = g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
        return SUCCEEDED(hr) && g_rtv != nullptr;
    }

    // --- шрифт с кириллицей ---
    void SetupFonts() {
        const wchar_t* candidates[] = {
            L"C:\\Windows\\Fonts\\segoeui.ttf",
            L"C:\\Windows\\Fonts\\arial.ttf",
        };
        ImFontConfig cfg{};
        cfg.PixelSnapH = true;
        cfg.FontDataOwnedByAtlas = false;
        // Встроенные icon fonts из предоставленного fonts.zip: DLL не зависит
        // от внешних файлов, что важно при инжекте external loader-ом.
        g_iconFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(gui_icons_ttf), static_cast<int>(gui_icons_ttf_size),
            16.0f, &cfg);
        ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(esp_icons_ttf), static_cast<int>(esp_icons_ttf_size),
            16.0f, &cfg);
        for (const wchar_t* path : candidates) {
            if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
                continue;
            char utf8[MAX_PATH * 2]{};
            WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
            ImFont* textFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
                utf8, 15.0f, &cfg, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
            if (textFont) {
                // Icon font добавляется первым, поэтому без явного FontDefault
                // ImGui пытался рисовать весь русский/латинский текст иконками.
                ImGui::GetIO().FontDefault = textFont;
                NW_LOG(L"шрифт меню: %s", path);
                return;
            }
        }
        NW_LOG(L"WARNING: системный шрифт не найден — меню будет с квадратами.");
    }

    bool InitImGui() {
        if (!g_device || !g_context || !g_window)
            return false;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        SetupFonts();
        if (!ImGui_ImplWin32_Init(g_window) || !ImGui_ImplDX11_Init(g_device, g_context))
            return false;
        g_imguiReady = true;
        NW_LOG(L"ImGui готов (device 0x%llX, окно 0x%llX)",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_device)),
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_window)));
        return true;
    }

    void ShutdownImGui() {
        if (!g_imguiReady)
            return;
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiReady = false;
    }

    // --- ESP: corner-box players через view matrix (adapted from CHEAT src). ---
    bool WorldToScreen(const ent::Vector3& world, ImVec2& out,
                       const float matrix[16], float width, float height) {
        const float w = matrix[3] * world.x + matrix[7] * world.y +
                        matrix[11] * world.z + matrix[15];
        if (w < 0.65f) return false;
        const float x = matrix[0] * world.x + matrix[4] * world.y +
                        matrix[8] * world.z + matrix[12];
        const float y = matrix[1] * world.x + matrix[5] * world.y +
                        matrix[9] * world.z + matrix[13];
        out.x = (width * 0.5f) + (width * 0.5f) * x / w;
        out.y = (height * 0.5f) - (height * 0.5f) * y / w;
        return true;
    }

    void DrawEsp(uintptr_t clientBase, uintptr_t entityList, uintptr_t localPlayer,
                 uint8_t localTeam) {
        if (!g_features.espEnabled.load())
            return;

        // Скан игроков дорогой (64 слота с безопасными чтениями) — делаем его
        // максимум 10 раз в секунду и кешируем мировые координаты. Раньше он
        // шёл каждый кадр в Present и ронял FPS. Если entity layout не
        // подтверждён рантаймом, скан не запускается вовсе.
        struct EspTarget {
            ent::Vector3 head, feet;
            int health;
        };
        static std::vector<EspTarget> cache;
        static DWORD lastScan = 0;
        const DWORD now = GetTickCount();
        if (now - lastScan >= 100) {
            lastScan = now;
            cache.clear();
            if (g_state.entityLayoutVerified.load() && entityList) {
                ent::ForEachPlayer(entityList, [&](const ent::PlayerSnapshot& player) {
                    if (player.pawn == localPlayer || player.team == localTeam || !player.IsAlive())
                        return;
                    cache.push_back({ { player.origin.x, player.origin.y, player.origin.z + 72.0f },
                                      player.origin,
                                      player.health });
                });
            }
        }

        float matrix[16];
        if (!mem::ReadArray<float>(clientBase + offsets::g.dwViewMatrix, matrix, 16))
            return;
        const float width = ImGui::GetIO().DisplaySize.x;
        const float height = ImGui::GetIO().DisplaySize.y;
        ImDrawList* draw = ImGui::GetBackgroundDrawList();

        for (const EspTarget& target : cache) {
            ImVec2 top, bottom;
            if (!WorldToScreen(target.head, top, matrix, width, height)) continue;
            if (!WorldToScreen(target.feet, bottom, matrix, width, height)) continue;
            const float boxHeight = bottom.y - top.y;
            if (boxHeight < 6.0f) continue;
            const float boxWidth = boxHeight * 0.45f;
            const float left = top.x - boxWidth * 0.5f;
            const float right = top.x + boxWidth * 0.5f;
            const ImU32 accent = IM_COL32(235, 60, 70, 230);
            const ImU32 shadow = IM_COL32(0, 0, 0, 200);
            draw->AddRect({left + 1, top.y + 1}, {right + 1, bottom.y + 1}, shadow, 0.f, 0, 2.0f);
            draw->AddRect({left, top.y}, {right, bottom.y}, accent, 0.f, 0, 1.5f);
            if (g_features.espHealth.load()) {
                const float fraction = std::clamp(target.health, 0, 100) / 100.0f;
                const ImU32 health = IM_COL32((int)(255 * (1.f - fraction)),
                                              (int)(210 * fraction), 50, 255);
                draw->AddRectFilled({left - 5.0f, bottom.y},
                                    {left - 2.0f, bottom.y - boxHeight * fraction}, health);
            }
        }
    }

    // --- меню: Neverwin в visual style MemeSense (sidebar + page cards). ---
    void RenderMenu() {
        if (!gui::g_menuOpen.load())
            return;

        const float w = ImGui::GetIO().DisplaySize.x;
        const float h = ImGui::GetIO().DisplaySize.y;
        ImGui::SetNextWindowSize(ImVec2(760.0f, 500.0f), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2((w - 760.0f) * 0.5f, (h - 500.0f) * 0.35f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.06f, 0.075f, 1.0f));
        ImGui::Begin("NEVERWIN", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);

        static int page = 0;
        const char* pages[] = { "Combat", "Movement", "Misc", "Settings" };
        const ImVec4 accent(0.94f, 0.16f, 0.25f, 1.0f);
        const float sidebarWidth = 180.0f;

        ImGui::BeginChild("##nw_sidebar", ImVec2(sidebarWidth, 0), false);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 sidePos = ImGui::GetWindowPos();
        draw->AddRectFilled(sidePos, ImVec2(sidePos.x + sidebarWidth, sidePos.y + 500.0f),
                            ImGui::GetColorU32(ImVec4(0.045f, 0.048f, 0.06f, 1.0f)));
        ImGui::SetCursorPos(ImVec2(24, 28));
        ImGui::TextColored(accent, "NEVER"); ImGui::SameLine(0, 0); ImGui::Text("WIN");
        ImGui::SetCursorPosX(24); ImGui::TextDisabled("premium internal");
        ImGui::SetCursorPosY(98);
        for (int i = 0; i < 4; ++i) {
            ImGui::PushStyleColor(ImGuiCol_Header, i == page ? ImVec4(accent.x, accent.y, accent.z, .18f) : ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(accent.x, accent.y, accent.z, .12f));
            ImGui::SetCursorPosX(12);
            if (ImGui::Selectable(pages[i], i == page, 0, ImVec2(156, 38))) page = i;
            ImGui::PopStyleColor(2);
        }
        ImGui::SetCursorPos(ImVec2(24, 455));
        ImGui::TextDisabled("tg: @fkfwj");
        ImGui::EndChild();

        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##nw_content", ImVec2(0, 0), false);
        ImGui::SetCursorPos(ImVec2(28, 28));
        ImGui::TextColored(accent, "%s", pages[page]);
        ImGui::Separator();
        ImGui::SetCursorPosX(28);
        ImGui::BeginChild("##nw_page", ImVec2(-28, -36), false);

        if (page == 0) {
            bool enabled = g_features.reverseAimEnabled.load();
            if (ImGui::Checkbox("Reverse aim enabled [F1]", &enabled)) g_features.reverseAimEnabled.store(enabled);
            int mode = g_features.reverseAimMode.load() - 1;
            const char* modes[] = { "raimv1", "raimv2", "test" };
            if (ImGui::Combo("Reverse aim mode", &mode, modes, 3)) g_features.reverseAimMode.store(mode + 1);
            float speed = g_features.reverseAimSpeed.load();
            if (ImGui::SliderFloat("Aim speed (deg/s)", &speed, 30.f, 8000.f, "%.0f")) g_features.reverseAimSpeed.store(speed);
            float smooth = g_features.reverseAimSmooth.load();
            if (ImGui::SliderFloat("Aim smooth (ms)", &smooth, 0.f, 500.f, "%.0f")) g_features.reverseAimSmooth.store(smooth);
            float pred = g_features.reverseAimPrediction.load();
            if (ImGui::SliderFloat("Position prediction (s)", &pred, 0.f, .35f, "%.3f")) g_features.reverseAimPrediction.store(pred);
            ImGui::Spacing(); ImGui::Separator();
            bool aa = g_features.antiAimless.load();
            if (ImGui::Checkbox("Antiaimless [F2]", &aa)) g_features.antiAimless.store(aa);
            float spin = g_features.spinSpeed.load();
            if (ImGui::SliderFloat("Spin speed", &spin, 0.f, 10.f, "x%.0f")) g_features.spinSpeed.store(spin);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(accent, "Ragebot");
            bool rage = g_features.ragebot.load();
            if (ImGui::Checkbox("Nonagon Ragebot [F6]", &rage)) g_features.ragebot.store(rage);
            if (rage) {
                bool trigger = g_features.rageAutoFire.load();
                if (ImGui::Checkbox("Triggerbot / auto fire", &trigger)) g_features.rageAutoFire.store(trigger);
                bool resolver = g_features.resolver.load();
                if (ImGui::Checkbox("Resolver", &resolver)) g_features.resolver.store(resolver);
                bool backtrack = g_features.backtrack.load();
                if (ImGui::Checkbox("Backtrack", &backtrack)) g_features.backtrack.store(backtrack);
                int fov = g_features.rageFov.load();
                if (ImGui::SliderInt("Rage FOV", &fov, 1, 180)) g_features.rageFov.store(fov);
                int hitchance = g_features.rageHitchance.load();
                if (ImGui::SliderInt("Hitchance", &hitchance, 0, 100)) g_features.rageHitchance.store(hitchance);
                int damage = g_features.rageMinDamage.load();
                if (ImGui::SliderInt("Minimum damage", &damage, 1, 100)) g_features.rageMinDamage.store(damage);
                ImGui::TextDisabled("Auto fire requires engine crosshair confirmation.");
            }
        } else if (page == 1) {
            bool bhop = g_features.bhop.load();
                if (ImGui::Checkbox("VeloBhop [F4]", &bhop)) g_features.bhop.store(bhop);
                ImGui::TextDisabled("Velocity CreateMove / CUserCmd path.");
        } else if (page == 2) {
            ImGui::TextColored(accent, "tg: @fkfwj");
            ImGui::Separator();
            bool clanTag = g_features.clanTag.load();
            if (ImGui::Checkbox("ClanTag [NeverWin]", &clanTag)) g_features.clanTag.store(clanTag);
            ImGui::TextDisabled("Animated tag + your captured normal nickname.");
            ImGui::Separator();
            bool esp = g_features.espEnabled.load();
            if (ImGui::Checkbox("ESP box", &esp)) g_features.espEnabled.store(esp);
            if (esp) {
                bool health = g_features.espHealth.load();
                if (ImGui::Checkbox("ESP health bar", &health)) g_features.espHealth.store(health);
            }
        } else {
            bool recoil = g_features.visualRecoil.load();
            if (ImGui::Checkbox("Visual recoil x4 [F3]", &recoil)) g_features.visualRecoil.store(recoil);
            bool gs = g_features.gamesense.load();
            if (ImGui::Checkbox("Gamesense [F5]", &gs)) g_features.gamesense.store(gs);
            ImGui::Separator();
            ImGui::Text("client.dll: 0x%llX", static_cast<unsigned long long>(g_state.clientBase.load()));
            ImGui::Text("Local HP: %d | Team: %d", g_state.localHealth.load(), g_state.localTeam.load());
            if (ImGui::Button("Detach / unload DLL", ImVec2(-1, 36))) gui::g_unloadRequested.store(true);
        }
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    // --- хук Present: рисуем меню перед отдачей кадра ---
    HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
        if (gui::g_unloadRequested.load()) {
            // Мы на рендер-потоке: снимаем всё и сигналим, чтобы поток фич
            // не освободил DLL, пока мы внутри.
            if (g_window && g_origWnd)
                SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWnd));
            ShutdownImGui();

            // возвращаем оригиналы в vtable свопчейна
            void** vt = *reinterpret_cast<void***>(sc);
            DWORD old = 0;
            VirtualProtect(vt, sizeof(void*) * 24, PAGE_READWRITE, &old);
            vt[kPresentIdx] = reinterpret_cast<void*>(g_origPresent);
            vt[kResizeIdx]  = reinterpret_cast<void*>(g_origResize);
            VirtualProtect(vt, sizeof(void*) * 24, old, &old);

            MH_DisableHook(MH_ALL_HOOKS);

            const HRESULT hr = g_origPresent(sc, sync, flags);
            SetEvent(g_eventUnhooked);
            return hr;
        }

        // quint: свежий RTV каждую сессию кадра, иначе ImGui рисует не в бэкбуфер.
        if (!g_rtv && !EnsureRtv()) {
            return g_origPresent(sc, sync, flags);
        }
        if (g_context)
            g_context->OMSetRenderTargets(1, &g_rtv, nullptr);

        if (!g_imguiReady && !InitImGui()) {
            return g_origPresent(sc, sync, flags);
        }

        if (g_imguiReady && g_context) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            RenderMenu();
            DrawEsp(g_state.clientBase.load(), g_state.entityList.load(),
                    g_state.localPlayer.load(), static_cast<uint8_t>(g_state.localTeam.load()));
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        return g_origPresent(sc, sync, flags);
    }

    // --- хук ResizeBuffers: RTV устарел, оригинал вперёд ---
    HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
        IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT fl) {
        DestroyRtv();
        return g_origResize(sc, count, w, h, fmt, fl);
    }

    // --- InputSystem::IsRelativeMouseMode: открыто меню — курсор наш ---
    void* __fastcall HookedRelMouse(void* input, bool active) {
        return g_origRelMouse(input, gui::g_menuOpen.load() ? false : active);
    }

    // --- WndProc: тоггл меню, ввод в ImGui, глотание клавиш ---
    LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        const bool menu = gui::g_menuOpen.load();

        // Тоггл по отпусканию? Нет — по нажатию, без автоповтора.
        if ((wParam == VK_INSERT || wParam == 'P') &&
            (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) &&
            !(lParam & (1u << 30))) {
            gui::g_menuOpen.store(!menu);
        }

        if (menu && g_imguiReady) {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

            if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN || msg == WM_KEYUP) {
                const bool moveKey =
                    wParam == 'R' || wParam == '1' || wParam == '2' ||
                    wParam == '3' || wParam == '4' || wParam == '5' ||
                    wParam == 'W' || wParam == 'A' || wParam == 'S' ||
                    wParam == 'D' || wParam == VK_SHIFT || wParam == VK_CONTROL ||
                    wParam == VK_TAB || wParam == VK_SPACE;
                if (!moveKey || ImGui::GetIO().WantTextInput)
                    return 1; // меню ест клавишу, игра не бегает
            }
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_MOUSEMOVE)
                return 0;
        }

        return CallWindowProcW(g_origWnd, hwnd, msg, wParam, lParam);
    }
}

namespace gui {

    bool Init() {
        g_eventUnhooked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (MH_Initialize() != MH_OK) {
            NW_LOG(L"MinHook не инициализировался — меню только в оверлее.");
            return false;
        }

        // Хук CreateMove удалён: на твоём клиенте он не встал (см. neverwin.log),
        // а F1/F2 вернулись на прямые записи viewAngles из цикла фич.

        // Свопчейн может появиться чуть позже DLL (инжект во время загрузки).
        for (int i = 0; i < 300 && !g_swapChain; ++i) {
            if (GrabSwapChain())
                break;
            Sleep(100);
        }
        if (!g_swapChain) {
            NW_LOG(L"WARNING: свопчейн игры не найден (сигнатура rendersystemdx11 не совпала или Vulkan).");
            NW_LOG(L"         меню будет в оверлее neverwin_overlay_vN.exe, фичи работают.");
            return false;
        }
        NW_LOG(L"свопчейн игры: 0x%llX",
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(g_swapChain)));

        if (FAILED(g_swapChain->GetDevice(__uuidof(ID3D11Device),
                                          reinterpret_cast<void**>(&g_device))) || !g_device) {
            NW_LOG(L"GetDevice провалился — меню только в оверлее.");
            return false;
        }
        g_device->GetImmediateContext(&g_context);

        void** vt = *reinterpret_cast<void***>(g_swapChain);
        if (MH_CreateHook(reinterpret_cast<LPVOID>(vt[kPresentIdx]),
                          reinterpret_cast<LPVOID>(&HookedPresent),
                          reinterpret_cast<LPVOID*>(&g_origPresent)) != MH_OK ||
            MH_CreateHook(reinterpret_cast<LPVOID>(vt[kResizeIdx]),
                          reinterpret_cast<LPVOID>(&HookedResizeBuffers),
                          reinterpret_cast<LPVOID*>(&g_origResize)) != MH_OK) {
            NW_LOG(L"MinHook на Present/ResizeBuffers не встал — меню только в оверлее.");
            return false;
        }

        // InputSystem::IsRelativeMouseMode — освобождение мыши в меню.
        HMODULE isMod = GetModuleHandleW(L"inputsystem.dll");
        if (isMod) {
            const auto createInterface = reinterpret_cast<void* (*)(const char*, int*)>(
                GetProcAddress(isMod, "CreateInterface"));
            void* input = createInterface ? createInterface("InputSystemVersion001", nullptr) : nullptr;
            if (input) {
                void** ivt = *reinterpret_cast<void***>(input);
                if (MH_CreateHook(reinterpret_cast<LPVOID>(ivt[kRelMouseIdx]),
                                  reinterpret_cast<LPVOID>(&HookedRelMouse),
                                  reinterpret_cast<LPVOID*>(&g_origRelMouse)) != MH_OK) {
                    NW_LOG(L"WARNING: IsRelativeMouseMode не захучен — мышь в меню может не слушаться.");
                }
            } else {
                NW_LOG(L"WARNING: InputSystemVersion001 не найден — мышь в меню может не слушаться.");
            }
        }

        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            NW_LOG(L"MinHook не встал (enable) — меню только в оверлее.");
            return false;
        }

        g_window = FindGameWindow();
        if (!g_window) {
            NW_LOG(L"окно игры не найдено — WndProc не захучен.");
            return false;
        }
        g_origWnd = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc)));
        if (!g_origWnd) {
            NW_LOG(L"WndProc не захучен — меню не откроется.");
            return false;
        }

        g_inGameMenuReady.store(true);
        NW_LOG(L"меню в игре готово (Present+Resize+RelMouse+WndProc). P/INSERT — открыть.");
        return true;
    }

    void ShutdownAndExit(HMODULE hModule) {
        g_unloadRequested.store(true);
        NW_LOG(L"выгрузка: жду, пока рендер-поток снимет хуки...");

        if (g_eventUnhooked)
            WaitForSingleObject(g_eventUnhooked, 3000);

        // Небольшой запас: дать рендер-потоку гарантированно выйти из нашего кода.
        Sleep(200);
        MH_Uninitialize();

        NW_LOG(L"выгружаю DLL.");
        FreeLibraryAndExitThread(hModule, 0);
    }
}
