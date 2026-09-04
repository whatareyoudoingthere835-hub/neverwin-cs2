#include "pch.h"
#include <chrono>
#include <algorithm>
#include "gui.hpp"
#include "features.hpp"
#include "log.hpp"
#include "memory.hpp"
#include "entities.hpp"
#include "offsets.hpp"
#include "velocity.hpp"
#include "assets/gui_icons.hpp"
#include "assets/esp_icons.hpp"
#include "assets/fa_solid.hpp"
#include "assets/icons_fontawesome.h"

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
    ImFont* g_iconFont = nullptr;   // Font Awesome (иконки вкладок)
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
        // Font Awesome Solid из MemeSense-набора: иконки вкладок по кодпоинтам
        // U+E000..U+F8FF (PUA). Текстовым шрифтом остаётся системный Segoe/Arial
        // с кириллицей — прошлый баг «иконки вместо текста» был из-за обратного.
        static const ImWchar iconRanges[] = { 0xE000, 0xF8FF, 0 };
        g_iconFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(fa_solid_ttf), static_cast<int>(fa_solid_ttf_size),
            16.0f, &cfg, iconRanges);
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
        // Диагностика гейта: три условия «не готово» раньше уходили молча —
        // в логе ноль строк и нельзя отличить «тумблер не включён» от
        // «localPlayer=0». Теперь: при включении «ESP box» — ОДНА сводная
        // строка со всеми входами; пока вход сломан — warning каждые 5 с.
        // (Если in-game меню не встало, DrawEsp вообще не вызывается —
        // тот случай логируется из feature loop, см. features.cpp.)
        static bool wasEnabled = false;
        static std::chrono::steady_clock::time_point lastGateWarn{};
        const bool espOn = g_features.espEnabled.load();
        if (espOn && !wasEnabled) {
            NW_LOG(L"esp: включён. localPlayer=0x%llX localTeam=%d layout=%s clientBase=0x%llX entityList=0x%llX",
                   static_cast<unsigned long long>(localPlayer), (int)localTeam,
                   g_state.entityLayoutVerified.load() ? L"verified" : L"FALLBACK",
                   static_cast<unsigned long long>(clientBase),
                   static_cast<unsigned long long>(entityList));
        }
        const auto gateNow = std::chrono::steady_clock::now();
        if (espOn && (localPlayer == 0 || clientBase == 0) &&
            gateNow - lastGateWarn >= std::chrono::seconds(5)) {
            lastGateWarn = gateNow;
            if (localPlayer == 0)
                NW_LOG(L"esp: localPlayer=0 — локального павна нет (лобби/загрузка?) либо стухший dwLocalPlayerPawn (0x%llX) — проверь neverwin.ini.",
                       static_cast<unsigned long long>(offsets::g.dwLocalPlayerPawn));
            else
                NW_LOG(L"esp: clientBase=0 — потеряна база client.dll.");
        }
        wasEnabled = espOn;
        if (!espOn || localPlayer == 0 || clientBase == 0)
            return;

        // Скан игроков дорогой (64 слота с безопасными чтениями) — 50 мс (20 Гц),
        // не каждый кадр. Если entity layout не подтверждён рантаймом, скан
        // не запускается вовсе.
        struct EspTarget {
            ent::Vector3 head, feet;
            int health;
            float distance;
        };
        static std::vector<EspTarget> cache;
        static std::chrono::steady_clock::time_point lastScan{};
        static std::chrono::steady_clock::time_point lastDiag{};
        static int diagEnemies = 0, diagTooFar = 0;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastScan >= std::chrono::milliseconds(50)) {
            lastScan = now;
            cache.clear();
            if (g_state.entityLayoutVerified.load() && entityList) {
                const float lx = g_state.localOriginX.load();
                const float ly = g_state.localOriginY.load();
                const float lz = g_state.localOriginZ.load();
                // Дальность и «своих рисуем или нет» — из меню.
                const float maxDist = g_features.espMaxDistance.load();
                const bool drawTeammates = g_features.espTeammates.load();
                int enemies = 0, tooFar = 0;
                ent::ForEachPlayer(entityList, [&](const ent::PlayerSnapshot& player) {
                    if (player.pawn == localPlayer || !player.IsAlive())
                        return;
                    if (!drawTeammates && player.team == localTeam)
                        return;
                    if (player.team != 0 && player.team != localTeam)
                        ++enemies;
                    const float dx = player.origin.x - lx;
                    const float dy = player.origin.y - ly;
                    const float dz = player.origin.z - lz;
                    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist > maxDist) {
                        ++tooFar;
                        return;
                    }
                    cache.push_back({ { player.origin.x, player.origin.y, player.origin.z + 72.0f },
                                      player.origin,
                                      player.health,
                                      dist });
                });
                diagEnemies = enemies;
                diagTooFar = tooFar;
                // Дальние рисуем первыми, ближние поверх них.
                std::sort(cache.begin(), cache.end(),
                          [](const EspTarget& a, const EspTarget& b) { return a.distance > b.distance; });
            }
        }

        // Диагностика «ESP что-то рисует / не рисует»: если включён, но
        // боксов нет, раз в 2 сек пишем в лог, ПОЧЕМУ.
        if (cache.empty() && now - lastDiag >= std::chrono::seconds(2)) {
            lastDiag = now;
            if (!g_state.entityLayoutVerified.load())
                NW_LOG(L"esp: ничего не рисую — entity layout не подтверждён рантаймом (строка 'entity-list:' в логе). До подтверждения боксов не будет, а fallback-оффсеты могут рисовать мусорные боксы «не на игроках».");
            else if (g_state.localTeam.load() == 0)
                NW_LOG(L"esp: ничего не рисую — local team не прочитан (вне матча?).");
            else
                NW_LOG(L"esp: layout ok, целей в радиусе %.0f м нет (врагов вокруг %d, вне радиуса %d). Своих включи 'ESP teammates'.",
                       g_features.espMaxDistance.load() / 52.49f, diagEnemies, diagTooFar);
        }

        float matrix[16];
        // dwViewMatrix может протечь при смене билда — pattern-fallback
        // (velocity: адрес матрицы напрямую, без clientBase).
        if (!mem::ReadArray<float>(clientBase + offsets::g.dwViewMatrix, matrix, 16) ||
            !(std::isfinite(matrix[0]) && std::isfinite(matrix[15]) &&
              (matrix[0] != 0.0f || matrix[1] != 0.0f))) {
            if (!velo::Globals().viewMatrix ||
                !mem::ReadArray<float>(velo::Globals().viewMatrix, matrix, 16))
                return;
        }
        // Здороовье view matrix: первая строка — единичный вектор камеры.
        // Если dwViewMatrix стух (обновили CS2 без ini), W2S проецирует в
        // мусор — боксы «не на игроках». Логируем один раз.
        static bool matrixWarned = false;
        if (!matrixWarned) {
            const float row0Len = std::sqrtf(matrix[0] * matrix[0] +
                                              matrix[1] * matrix[1] +
                                              matrix[2] * matrix[2]);
            if (row0Len < 0.5f || row0Len > 1.5f) {
                matrixWarned = true;
                NW_LOG(L"WARNING esp: view matrix выглядит битой (|row0|=%.2f) — боксы будут не на игроках. Обнови neverwin.ini (dwViewMatrix).", row0Len);
            }
        }
        const float width = ImGui::GetIO().DisplaySize.x;
        const float height = ImGui::GetIO().DisplaySize.y;
        ImDrawList* draw = ImGui::GetBackgroundDrawList();

        for (const EspTarget& target : cache) {
            ImVec2 top, bottom;
            if (!WorldToScreen(target.head, top, matrix, width, height)) continue;
            if (!WorldToScreen(target.feet, bottom, matrix, width, height)) continue;
            const float boxHeight = bottom.y - top.y;
            if (boxHeight < 6.0f) continue;
            const float boxWidth = boxHeight * 0.38f;
            const float left = top.x - boxWidth * 0.5f;
            const float right = top.x + boxWidth * 0.5f;

            const float healthFrac = std::clamp(target.health, 0, 100) / 100.0f;
            const ImU32 color = IM_COL32((int)(255 * (1.0f - healthFrac)),
                                         (int)(255 * healthFrac), 50, 230);
            const ImU32 shadow = IM_COL32(0, 0, 0, 180);

            draw->AddRect({left + 1, top.y + 1}, {right + 1, bottom.y + 1}, shadow, 0.f, 0, 2.0f);
            draw->AddRect({left, top.y}, {right, bottom.y}, color, 0.f, 0, 1.5f);

            if (g_features.espHealth.load()) {
                draw->AddRectFilled({left - 5.0f, bottom.y},
                                    {left - 2.0f, bottom.y - boxHeight * healthFrac}, color);
            }
            if (g_features.espDistance.load()) {
                char distText[32];
                snprintf(distText, sizeof(distText), "%.0fm", target.distance / 52.49f);
                draw->AddText({left, bottom.y + 2.0f}, IM_COL32(255, 255, 255, 200), distText);
            }
        }
    }

    // --- меню: полная структура MemeSense (RGB-полоска, sidebar с иконками
    // FA, заголовок + Save в правом углу). Название чита — NEVERWIN. ---
    void DrawRgbStrip(ImDrawList* draw, const ImVec2& pos, float width, float height) {
        // 2px анимированная радужная полоса на весь верх окна.
        const float t = static_cast<float>(ImGui::GetTime());
        for (int x = 0; x < static_cast<int>(width); ++x) {
            const float hue = fmodf((t * 0.08f) + static_cast<float>(x) / width, 1.0f);
            ImVec4 col = ImColor::HSV(hue, 0.85f, 1.0f).Value;
            draw->AddRectFilled({pos.x + static_cast<float>(x), pos.y},
                                {pos.x + static_cast<float>(x) + 1.0f, pos.y + height},
                                ImGui::GetColorU32(col));
        }
    }

    void RenderMenu() {
        if (!gui::g_menuOpen.load())
            return;

        const float w = ImGui::GetIO().DisplaySize.x;
        const float h = ImGui::GetIO().DisplaySize.y;
        const ImVec2 menuSize(780.0f, 540.0f);
        ImGui::SetNextWindowSize(menuSize, ImGuiCond_Always);
        // Клампим позицию: окно целиком внутри игрового окна, sidebar слева
        // не вылезает за экран на мелких разрешениях.
        const float mx = std::clamp((w - menuSize.x) * 0.5f, 0.0f, std::max(0.0f, w - menuSize.x));
        const float my = std::clamp((h - menuSize.y) * 0.35f, 0.0f, std::max(0.0f, h - menuSize.y));
        ImGui::SetNextWindowPos(ImVec2(mx, my), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f)); // #1a1a1a
        ImGui::Begin("NEVERWIN", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoScrollbar);

        static int page = 0; // 0 Combat ... по табличке ниже
        struct TabDef { const char* name; const char* icon; };
        static const TabDef tabs[] = {
            { "Legitbot",       ICON_FA_CROSSHAIRS },
            { "Aim Assist",     ICON_FA_COMPUTER_MOUSE },
            { "Players",        ICON_FA_USER },
            { "Chams",          ICON_FA_USER_ASTRONAUT },
            { "Items",          ICON_FA_GUN },
            { "Visuals",        ICON_FA_FIRE },
            { "World",          ICON_FA_GLOBE },
            { "View",           ICON_FA_CAMERA },
            { "Indicators",     ICON_FA_CHART_LINE },
            { "Misc",           ICON_FA_BARS_STAGGERED },
            { "Movement",       ICON_FA_PERSON_RUNNING },
            { "Inventory",      ICON_FA_PAINTBRUSH },
            { "Configs",        ICON_FA_FOLDER_OPEN },
        };
        constexpr int kTabCount = sizeof(tabs) / sizeof(tabs[0]);
        const ImVec4 accent(0.94f, 0.16f, 0.25f, 1.0f);
        const float sidebarWidth = 186.0f;
        const float headerHeight = 42.0f;
        const float stripHeight = 2.0f;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        DrawRgbStrip(draw, winPos, menuSize.x, stripHeight);

        // --- Заголовок: имя + Save в правом углу ---
        ImGui::SetCursorPos(ImVec2(sidebarWidth + 20.0f, 12.0f));
        ImGui::PushFont(nullptr);
        ImGui::TextColored(ImVec4(1, 1, 1, 0.9f), "NEVERWIN");
        ImGui::SameLine();
        ImGui::TextDisabled("cs2");
        ImGui::SetCursorPos(ImVec2(menuSize.x - 96.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
        if (ImGui::Button("Save", ImVec2(80.0f, 26.0f))) {
            // TODO: сериализация конфига — пока просто лог.
            NW_LOG(L"config: Save нажат (сериализация пока не реализована).");
        }
        ImGui::PopStyleColor(2);
        ImGui::PopFont();

        // --- Sidebar ---
        ImGui::SetCursorPos(ImVec2(0, stripHeight));
        ImGui::BeginChild("##nw_sidebar", ImVec2(sidebarWidth, menuSize.y - stripHeight), false);
        draw->AddRectFilled({winPos.x, winPos.y + stripHeight},
                            {winPos.x + sidebarWidth, winPos.y + menuSize.y},
                            ImGui::GetColorU32(ImVec4(0.085f, 0.085f, 0.09f, 1.0f)));
        ImGui::SetCursorPos(ImVec2(0, 8.0f));
        const ImVec4 tabTextDim(0.62f, 0.62f, 0.66f, 1.0f);
        const ImVec4 tabHover(0.14f, 0.14f, 0.15f, 1.0f);
        for (int i = 0; i < kTabCount; ++i) {
            const bool active = (i == page);
            const ImVec2 rowPos = ImGui::GetCursorScreenPos();
            const float rowH = 30.0f;
            const float rowW = sidebarWidth - 4.0f; // отступ справа: строки
            // не доходят до края окна/контента (починили «вылезание»).
            if (active)
                draw->AddRectFilled(rowPos, {rowPos.x + 2.5f, rowPos.y + rowH},
                                    ImGui::GetColorU32(accent));
            if (ImGui::IsMouseHoveringRect(rowPos, {rowPos.x + rowW, rowPos.y + rowH}) && !active)
                draw->AddRectFilled(rowPos, {rowPos.x + rowW, rowPos.y + rowH},
                                    ImGui::GetColorU32(tabHover));
            ImGui::PushID(i);
            const bool tabClicked = ImGui::InvisibleButton("##nw_tab", ImVec2(rowW, rowH));
            ImGui::PopID();
            if (tabClicked)
                page = i;
            // Иконка рисуется ШРИФТОМ Font Awesome. Раньше кодпоинт иконки
            // (U+F0xx) уходил в текстовый шрифт (Segoe), у которого этих
            // глифов нет — ImGui рисовал FallbackChar U+FFFD, то самый
            // «ромб с вопросом».
            const ImVec4 textColor = active ? accent : tabTextDim;
            if (g_iconFont) {
                ImGui::PushFont(g_iconFont);
                ImGui::SetCursorScreenPos({rowPos.x + 12.0f, rowPos.y + (rowH - 16.0f) * 0.5f});
                ImGui::TextColored(textColor, "%s", tabs[i].icon);
                ImGui::PopFont();
            }
            ImGui::SetCursorScreenPos({rowPos.x + 40.0f, rowPos.y + (rowH - 15.0f) * 0.5f});
            ImGui::TextColored(textColor, "%s", tabs[i].name);
        }
        ImGui::SetCursorPos(ImVec2(14.0f, menuSize.y - stripHeight - 26.0f));
        ImGui::TextDisabled("tg: @fkfwj");
        ImGui::EndChild();

        // --- Content ---
        ImGui::SameLine(0, 0);
        ImGui::BeginChild("##nw_content", ImVec2(0, 0), false);
        ImGui::SetCursorPos(ImVec2(22.0f, headerHeight));
        ImGui::BeginChild("##nw_page", ImVec2(-22.0f, -30.0f), false);

        // Страницы маппим на наши реальные функции.
        if (page == 0 || page == 1) {
            // Legitbot / Aim Assist
            bool enabled = g_features.reverseAimEnabled.load();
            if (ImGui::Checkbox("Reverse aim enabled [F1]", &enabled)) g_features.reverseAimEnabled.store(enabled);
            int mode = g_features.reverseAimMode.load() - 1;
            const char* modes[] = { "raimv1", "raimv2", "test" };
            if (ImGui::Combo("Reverse aim mode", &mode, modes, 3)) g_features.reverseAimMode.store(mode + 1);
            float speed = g_features.reverseAimSpeed.load();
            if (ImGui::SliderFloat("Aim speed (deg/s)", &speed, 30.f, 8000.f, "%.0f")) g_features.reverseAimSpeed.store(speed);
            float smooth = g_features.reverseAimSmooth.load();
            if (ImGui::SliderFloat("Aim smooth (ms)", &smooth, 0.f, 500.f, "%.0f")) g_features.reverseAimSmooth.store(smooth);
            int rate = g_features.reverseAimRate.load();
            if (ImGui::SliderInt("Aim updates per second", &rate, 1, 120)) g_features.reverseAimRate.store(rate);
            ImGui::TextDisabled("Warning: high update rates may cause lag.");
            float pred = g_features.reverseAimPrediction.load();
            if (ImGui::SliderFloat("Position prediction (s)", &pred, 0.f, .35f, "%.3f")) g_features.reverseAimPrediction.store(pred);
            // Lagcomp (velocity): цель симулируется до серверного тика.
            // Включён по умолчанию — он и есть фикс «аим позади головы».
            bool ext = g_features.extrapolation.load();
            if (ImGui::Checkbox("Extrapolation (lagcomp)", &ext)) g_features.extrapolation.store(ext);
            if (ext)
                ImGui::TextDisabled("When active it replaces the prediction slider.");
            bool trig = g_features.reverseAimTrigger.load();
            if (ImGui::Checkbox("Triggerbot", &trig)) g_features.reverseAimTrigger.store(trig);
            bool silent = g_features.silentAim.load();
            if (ImGui::Checkbox("Silent aim (usercmd only)", &silent)) g_features.silentAim.store(silent);
            bool ns = g_features.noSpread.load();
            if (ImGui::Checkbox("NoSpread (aim + rage)", &ns)) g_features.noSpread.store(ns);
        } else if (page == 2 || page == 3) {
            // Players / Chams — сканер и цели
            ImGui::TextColored(accent, "Players");
            ImGui::Separator();
            ImGui::Text("client.dll: 0x%llX", static_cast<unsigned long long>(g_state.clientBase.load()));
            ImGui::Text("Local: 0x%llX (hp %d, team %d)",
                        static_cast<unsigned long long>(g_state.localPlayer.load()),
                        g_state.localHealth.load(), g_state.localTeam.load());
            ImGui::Text("Entity layout: %s", g_state.entityLayoutVerified.load() ? "verified" : "fallback");
            ImGui::TextDisabled("Chams требуют hook материалов — пока не реализованы.");
        } else if (page == 4 || page == 5) {
            // Items / Visuals
            ImGui::TextColored(accent, "Visuals");
            ImGui::Separator();
            bool recoil = g_features.visualRecoil.load();
            if (ImGui::Checkbox("Visual recoil x4 [F3]", &recoil)) g_features.visualRecoil.store(recoil);
            bool gs = g_features.gamesense.load();
            if (ImGui::Checkbox("Gamesense [F5]", &gs)) g_features.gamesense.store(gs);
        } else if (page == 6 || page == 7) {
            // World / View
            ImGui::TextColored(accent, "World / View");
            ImGui::Separator();
            ImGui::TextDisabled("Skybox / fog / FOV — в планах.");
            bool esp = g_features.espEnabled.load();
            if (ImGui::Checkbox("ESP box", &esp)) g_features.espEnabled.store(esp);
            bool health = g_features.espHealth.load();
            if (ImGui::Checkbox("ESP health bar", &health)) g_features.espHealth.store(health);
            bool dist = g_features.espDistance.load();
            if (ImGui::Checkbox("ESP distance", &dist)) g_features.espDistance.store(dist);
            // Дальность: был хардкод 3000 юнитов (~57 м) — на картах CS2
            // враги чаще дальше, из-за чего ESP «то что-то рисует, то нет».
            int espDistM = (int)(g_features.espMaxDistance.load() / 52.49f);
            if (ImGui::SliderInt("ESP distance (m)", &espDistM, 25, 400))
                g_features.espMaxDistance.store(espDistM * 52.49f);
            bool tm = g_features.espTeammates.load();
            if (ImGui::Checkbox("ESP teammates", &tm)) g_features.espTeammates.store(tm);
        } else if (page == 8) {
            // Indicators
            ImGui::TextColored(accent, "Indicators");
            ImGui::Separator();
            ImGui::Text("HP: %d", g_state.localHealth.load());
            ImGui::Text("Team: %d", g_state.localTeam.load());
            ImGui::Text("Offsets: %s", g_state.offsetsFromIni.load() ? "ini" : "built-in");
        } else if (page == 9) {
            // Misc
            ImGui::TextColored(accent, "Misc");
            ImGui::Separator();
            bool aa = g_features.antiAimless.load();
            if (ImGui::Checkbox("Antiaimless [F2] (silent)", &aa)) g_features.antiAimless.store(aa);
            ImGui::TextDisabled("Silent: камера не двигается, «в пол + спин» идёт в usercmd.");
            bool los = g_features.antiaimlessLos.load();
            if (ImGui::Checkbox("Spin only when visible (LOS)", &los)) g_features.antiaimlessLos.store(los);
            if (los)
                ImGui::TextDisabled("Spin breaks your own aim; only spin on real line-of-sight.");
            // Скорость — ГРАДУСЫ В СЕКУНДУ (насколько быстро крутить),
            // интегрируется по времени; не «шаг за итерацию цикла».
            float spin = g_features.spinSpeed.load();
            if (ImGui::SliderFloat("Spin speed (deg/s)", &spin, 10.f, 3600.f, "%.0f")) g_features.spinSpeed.store(spin);
            bool clanTag = g_features.clanTag.load();
            if (ImGui::Checkbox("ClanTag [NeverWin]", &clanTag)) g_features.clanTag.store(clanTag);
            bool rage = g_features.ragebot.load();
            if (ImGui::Checkbox("Nonagon Ragebot [F6]", &rage)) g_features.ragebot.store(rage);
            if (rage) {
                bool autoFire = g_features.rageAutoFire.load();
                if (ImGui::Checkbox("Auto fire / trigger", &autoFire)) g_features.rageAutoFire.store(autoFire);
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
            }
        } else if (page == 10) {
            // Movement
            bool bhop = g_features.bhop.load();
            if (ImGui::Checkbox("VeloBhop [F4]", &bhop)) g_features.bhop.store(bhop);
            bool ext = g_features.extHope.load();
            if (ImGui::Checkbox("ExtHope (hold X)", &ext)) g_features.extHope.store(ext);
            int extRate = g_features.extHopeRate.load();
            if (ImGui::SliderInt("ExtHope jumps per second", &extRate, 1, 128)) g_features.extHopeRate.store(extRate);
            ImGui::TextDisabled("Velocity CreateMove / CUserCmd path.");
        } else if (page == 11) {
            // Inventory
            ImGui::TextColored(accent, "Inventory");
            ImGui::Separator();
            ImGui::TextDisabled("Skin changer — в планах (нужен econ item system).");
        } else {
            // Configs
            ImGui::TextColored(accent, "Configs");
            ImGui::Separator();
            ImGui::TextDisabled("Сохранение/загрузка конфигов — в планах.");
            if (ImGui::Button("Detach / unload DLL", ImVec2(-1, 36))) gui::g_unloadRequested.store(true);
            ImGui::TextDisabled("v%d | P/INSERT menu | END unload", NW_VERSION);
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

        // Примечание: хук CreateMove ставится НЕ здесь, а из feature loop
        // (TryHookCreateMove, features.cpp) — до цикла фич.

        // Свопчейн может появиться чуть позже DLL (инжект во время загрузки).
        for (int i = 0; i < 300 && !g_swapChain; ++i) {
            if (GrabSwapChain())
                break;
            Sleep(100);
        }
        if (!g_swapChain) {
            NW_LOG(L"WARNING: свопчейн игры не найден (сигнатура rendersystemdx11 не совпала или Vulkan).");
            NW_LOG(L"         in-game меню и ESP недоступны (оверлея в проекте нет), фичи работают.");
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
