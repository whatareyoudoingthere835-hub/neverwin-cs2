// ============================================================================
// NEVERWIN — внешний оверлей: меню и HUD на DirectX 12 (ImGui + D3D12).
//
// Ничего не инжектит: собственное окно (topmost, click-through когда меню
// закрыто) поверх игры, состояние и команды — через shared memory
// (shared_state.hpp). Работает при любом рендерере CS2 — DX11 и Vulkan.
// Игра должна быть в оконном/borderless-режиме: поверх exclusive fullscreen
// внешнее окно не встанет.
//
// Управление:
//   P  — открыть/закрыть меню (кнопку читает DLL, оверлей отражает)
//   F6 — скрыть/показать HUD
//   Чекбоксы в меню — реальные тогглы фич (команды уходят в DLL).
//   После END (выгрузки DLL) оверлей показывает "DLL выгружена" и выходит.
//
// Окно без WS_EX_LAYERED: прозрачность через DWM per-pixel alpha
// (DXGI_ALPHA_MODE_PREMULTIPLIED), фон очищается в (0,0,0,0).
// ============================================================================
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_2.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

#include <cstdio>
#include <cstring>

#include "shared_state.hpp"

#ifndef IID_PPV_ARGS
#define IID_PPV_ARGS(ppType) __uuidof(**(ppType)), reinterpret_cast<void**>(ppType)
#endif

// В imgui 1.93 WIP декларация WndProcHandler убрана из заголовка в #if 0
// (чтобы не тащить windows.h) — объявляем сами, функция не static.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

    constexpr int    kWidth      = 440;
    constexpr int    kHeight     = 350;
    constexpr int    kPosX       = 14;
    constexpr int    kPosY       = 14;
    constexpr UINT   kNumFrames  = 3; // свопчейн-буферов и кадров в полёте

    struct FrameCtx {
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12Fence*            fence     = nullptr;
        UINT64                  fenceVal  = 0;
        HANDLE                  event     = nullptr;
    };

    HWND                    g_hwnd      = nullptr;
    IDXGISwapChain1*        g_sc        = nullptr;
    ID3D12Device*           g_dev       = nullptr;
    ID3D12CommandQueue*     g_queue     = nullptr;
    ID3D12GraphicsCommandList* g_cmd       = nullptr;
    ID3D12DescriptorHeap*   g_rtvHeap   = nullptr;
    ID3D12DescriptorHeap*   g_srvHeap   = nullptr;
    UINT                    g_rtvSize   = 0;
    FrameCtx                g_frames[kNumFrames];
    UINT                    g_frameIdx  = 0;
    UINT64                  g_fenceCnt  = 0;

    // Локальные зеркала фич: чтобы чекбоксы не дёргались, пока команда
    // летит до DLL и назад.
    struct MenuMirror {
        bool     aa = false, al = false, vr = false, ab = false, gs = false;
        uint32_t pendingCmd = 0;
        bool     dirty = false;
    } g_m;

    bool g_clickThrough = true;

    // --- SRV-дескрипторы для текстур ImGui ---
    // Бэкенд 1.93 сам запрашивает дескриптор через колбэк (фонт и т.п.).
    // Кольцо: каждому кадру — своё окно из kSrvPerFrame дескрипторов; к моменту
    // возврата окна фенс гарантирует, что GPU дескрипторы уже не держит.
    constexpr UINT kSrvPerFrame = 4;
    UINT g_srvHeapCount = 0;
    UINT g_srvCursor    = 0;

    void SrvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                  D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        const UINT inc = g_dev->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT slot = (g_srvCursor++) % g_srvHeapCount;
        outCpu->ptr = g_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(slot) * inc;
        outGpu->ptr = g_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(slot) * inc;
    }
    void SrvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {
        // кольцо — освобождение не нужно
    }

    void SetClickThrough(bool on) {
        if (g_clickThrough == on)
            return;
        g_clickThrough = on;
        LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
        if (on) ex |= WS_EX_TRANSPARENT;
        else    ex &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex);
    }

    // --- шрифт с кириллицей (встроенный у ImGui русский не рисует) ---
    void SetupFonts() {
        const wchar_t* candidates[] = {
            L"C:\\Windows\\Fonts\\segoeui.ttf",
            L"C:\\Windows\\Fonts\\arial.ttf",
        };
        ImFontConfig cfg{};
        cfg.PixelSnapH = true;
        for (const wchar_t* path : candidates) {
            if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
                continue;
            char utf8[MAX_PATH * 2]{};
            WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
            ImGui::GetIO().Fonts->AddFontFromFileTTF(utf8, 16.0f, &cfg,
                                                     ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
            return;
        }
    }

    bool InitD3D12() {
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_dev)))) {
            // Фолбэк на WARP (софтвер) — меню будет жить даже без видеодрайвера.
            IDXGIFactory4* f4 = nullptr;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f4))) && f4) {
                IDXGIAdapter* warp = nullptr;
                if (SUCCEEDED(f4->EnumWarpAdapter(IID_PPV_ARGS(&warp))) && warp) {
                    D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_dev));
                    warp->Release();
                }
                f4->Release();
            }
        }
        if (!g_dev)
            return false;

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(g_dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_queue))))
            return false;

        IDXGIFactory2* factory2 = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory2))) || !factory2)
            return false;

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width        = kWidth;
        sd.Height       = kHeight;
        sd.Format       = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount  = kNumFrames;
        sd.Scaling      = DXGI_SCALING_STRETCH;
        sd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode    = DXGI_ALPHA_MODE_PREMULTIPLIED; // per-pixel прозрачность через DWM

        const HRESULT hr = factory2->CreateSwapChainForHwnd(
            g_queue, g_hwnd, &sd, nullptr, nullptr, &g_sc);
        factory2->Release();
        if (FAILED(hr))
            return false;

        // RTV-куча: по дескриптору на бэкбуфер.
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = kNumFrames;
        if (FAILED(g_dev->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap))))
            return false;
        g_rtvSize = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < kNumFrames; ++i) {
            ID3D12Resource* buf = nullptr;
            g_sc->GetBuffer(i, IID_PPV_ARGS(&buf));
            g_dev->CreateRenderTargetView(buf, nullptr, rtv);
            buf->Release();
            rtv.ptr += g_rtvSize;
        }

        // SRV-куча (shader visible) — для текстур ImGui (фонт и т.п.).
        g_srvHeapCount = kNumFrames * kSrvPerFrame;
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = g_srvHeapCount;
        srvDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(g_dev->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap))))
            return false;

        for (UINT i = 0; i < kNumFrames; ++i) {
            g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          IID_PPV_ARGS(&g_frames[i].allocator));
            g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_frames[i].fence));
            g_frames[i].event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
        if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            g_frames[0].allocator, nullptr,
                                            IID_PPV_ARGS(&g_cmd))))
            return false;
        g_cmd->Close(); // в полёте не нужен, кадры ресетят его сами

        return true;
    }

    bool InitImGuiD3D12() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGuiStyle& st = ImGui::GetStyle();
        st.WindowRounding  = 10.0f;
        st.FrameRounding   = 4.0f;
        st.WindowBorderSize = 0.0f;
        SetupFonts();

        if (!ImGui_ImplWin32_Init(g_hwnd))
            return false;

        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device              = g_dev;
        initInfo.CommandQueue        = g_queue;
        initInfo.NumFramesInFlight   = kNumFrames;
        initInfo.RTVFormat           = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.SrvDescriptorHeap   = g_srvHeap;
        initInfo.SrvDescriptorAllocFn = SrvAlloc;
        initInfo.SrvDescriptorFreeFn  = SrvFree;
        if (!ImGui_ImplDX12_Init(&initInfo))
            return false;

        // Фонт аллоцирует SRV из нашего кольца — курсор выставляем на окно кадра 0.
        g_srvCursor = 0;
        ImGui_ImplDX12_CreateDeviceObjects();
        return true;
    }

    void ShutdownImGuiD3D12() {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    // --- отрисовка ---
    void PushPanel(float alpha) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.06f, 0.08f, alpha));
    }
    void PopPanel() { ImGui::PopStyleColor(); }

    void DrawHud(const nwshared::State& st) {
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(416.0f, 250.0f), ImGuiCond_Always);
        PushPanel(0.88f);
        const ImGuiWindowFlags hudFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs;
        ImGui::Begin("hud", nullptr, hudFlags);

        ImGui::Text("NEVERWIN");
        ImGui::SameLine();
        ImGui::TextDisabled("v%d", static_cast<int>(st.dllVersion));
        ImGui::TextDisabled("professional software for losing professionally");
        ImGui::Separator();

        const struct { const char* name; bool on; } rows[] = {
            { "[F1] Реверс аимбот: наводка на тимейтов", st.antiAimbot != 0 },
            { "[F2] Антиаимлесс: взгляд в пол",           st.antiAimless != 0 },
            { "[F3] Visual recoil x4",                    st.visualRecoil != 0 },
            { "[F4] Антибхоп",                            st.antiBhop != 0 },
            { "[F5] Gamesense: дроп оружия",              st.gamesense != 0 },
        };
        for (const auto& r : rows) {
            ImGui::Text("%s", r.name);
            ImGui::SameLine();
            ImGui::TextColored(r.on ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                                    : ImVec4(0.85f, 0.42f, 0.40f, 1.0f),
                               r.on ? "ON" : "OFF");
        }
        ImGui::Separator();
        ImGui::TextDisabled("client.dll  0x%llX", static_cast<unsigned long long>(st.clientBase));
        ImGui::TextDisabled("LocalPlayer 0x%llX  hp %d  team %d",
                            static_cast<unsigned long long>(st.localPlayer),
                            static_cast<int>(st.localHealth), static_cast<int>(st.localTeam));
        ImGui::TextColored(st.offsetsFromIni ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f)
                                             : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                           st.offsetsFromIni ? "оффсеты: из neverwin.ini"
                                             : "оффсеты: ВСТРОЕННЫЕ — обнови ini!");
        ImGui::TextDisabled("P - меню | F6 - скрыть HUD | END - выгрузка");
        ImGui::End();
        PopPanel();
    }

    void DrawMenu(nwshared::Viewer& viewer, const nwshared::State& st) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        PushPanel(0.95f);
        const ImGuiWindowFlags menuFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("menu", nullptr, menuFlags);

        ImGui::Text("NEVERWIN  v%d", static_cast<int>(st.dllVersion));
        ImGui::TextDisabled("professional software for losing professionally");
        ImGui::Separator();
        ImGui::Spacing();

        const auto checkbox = [&](const char* label, uint32_t bit, bool& mirror) {
            if (ImGui::Checkbox(label, &mirror))
                g_m.pendingCmd = viewer.SendCommand(bit, mirror ? bit : 0);
        };
        checkbox("Реверс аимбот: наводка на тимейтов [F1]", nwshared::kFbAntiAimbot,   g_m.aa);
        checkbox("Антиаимлесс: взгляд в пол [F2]",          nwshared::kFbAntiAimless,  g_m.al);
        checkbox("Visual recoil x4 [F3]",                   nwshared::kFbVisualRecoil, g_m.vr);
        checkbox("Антибхоп [F4]",                           nwshared::kFbAntiBhop,     g_m.ab);
        checkbox("Gamesense: дроп оружия [F5]",             nwshared::kFbGamesense,    g_m.gs);

        ImGui::Separator();
        ImGui::TextDisabled("client.dll  0x%llX", static_cast<unsigned long long>(st.clientBase));
        ImGui::TextDisabled("LocalPlayer 0x%llX  hp %d  team %d",
                            static_cast<unsigned long long>(st.localPlayer),
                            static_cast<int>(st.localHealth), static_cast<int>(st.localTeam));
        ImGui::TextColored(st.offsetsFromIni ? ImVec4(0.45f, 1.0f, 0.55f, 1.0f)
                                             : ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                           st.offsetsFromIni ? "оффсеты: из neverwin.ini"
                                             : "оффсеты: ВСТРОЕННЫЕ — обнови ini!");
        ImGui::Spacing();

        if (ImGui::Button("Выгрузить DLL", ImVec2(-1, 0)))
            viewer.SendCommand(nwshared::kFbUnload, nwshared::kFbUnload);
        ImGui::TextDisabled("P - закрыть меню | F1-F5 тоже работают");
        ImGui::End();
        PopPanel();
    }

    void DrawStatus(const char* line, const char* sub, const ImVec4& color) {
        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(416.0f, 90.0f), ImGuiCond_Always);
        PushPanel(0.9f);
        ImGui::Begin("status", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(color, "%s", line);
        ImGui::TextDisabled("%s", sub);
        ImGui::End();
        PopPanel();
    }

    // Зеркала фич: синхронизируем со снапшотом, когда нет невыполненной команды.
    void SyncMirrors(const nwshared::State& st, nwshared::Viewer& viewer) {
        if (g_m.pendingCmd && viewer.IsApplied(g_m.pendingCmd))
            g_m.pendingCmd = 0;
        if (g_m.pendingCmd)
            return;
        g_m.aa = st.antiAimbot != 0;
        g_m.al = st.antiAimless != 0;
        g_m.vr = st.visualRecoil != 0;
        g_m.ab = st.antiBhop != 0;
        g_m.gs = st.gamesense != 0;
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return 0;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    enum class Mode { Waiting, Hud, Menu, Unloaded };

    void RenderFrame(nwshared::Viewer& viewer, const nwshared::State& st, Mode mode) {
        FrameCtx& fr = g_frames[g_frameIdx];
        if (fr.fence->GetCompletedValue() < fr.fenceVal) {
            ResetEvent(fr.event);
            fr.fence->SetEventOnCompletion(fr.fenceVal, fr.event);
            WaitForSingleObject(fr.event, INFINITE);
        }

        fr.allocator->Reset();
        g_cmd->Reset(fr.allocator, nullptr);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(g_frameIdx) * g_rtvSize;
        const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
        g_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        g_cmd->SetDescriptorHeaps(1, &g_srvHeap);

        // Окно SRV-дескрипторов текущего кадра: NewFrame/CreateDeviceObjects
        // аллоцируют из него через SrvAlloc.
        g_srvCursor = g_frameIdx * kSrvPerFrame;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::GetIO().MouseDrawCursor = (mode == Mode::Menu);

        switch (mode) {
        case Mode::Hud:   DrawHud(st); break;
        case Mode::Menu:  DrawMenu(viewer, st); break;
        case Mode::Unloaded:
            DrawStatus("DLL выгружена (END)", "оверлей закроется через пару секунд",
                       ImVec4(0.85f, 0.42f, 0.40f, 1.0f));
            break;
        case Mode::Waiting:
        default:
            DrawStatus("ожидание neverwin.dll...",
                       "инжектни DLL — меню откроется на P",
                       ImVec4(0.55f, 0.57f, 0.62f, 1.0f));
            break;
        }

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmd);

        g_cmd->Close();
        ID3D12CommandList* lists[] = { g_cmd };
        g_queue->ExecuteCommandLists(1, lists);
        g_sc->Present(1, 0);

        fr.fenceVal = ++g_fenceCnt;
        g_queue->Signal(fr.fence, fr.fenceVal);
        g_frameIdx = (g_frameIdx + 1) % kNumFrames;
    }

} // namespace

int wmain() {
    SetProcessDPIAware();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NeverwinOverlayWnd";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"neverwin overlay", WS_POPUP,
        kPosX, kPosY, kWidth, kHeight,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd)
        return 1;

    if (!InitD3D12() || !InitImGuiD3D12()) {
        MessageBoxW(nullptr, L"не смог поднять DirectX 12 / ImGui", L"neverwin overlay", MB_ICONERROR);
        return 1;
    }

    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));

    nwshared::Viewer viewer;
    nwshared::State  st{};
    ULONGLONG lastAttach = 0, unloadSeenAt = 0;
    bool show = true;

    ShowWindow(g_hwnd, SW_SHOWNA);

    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        const ULONGLONG now = GetTickCount64();

        if (!viewer.Alive() && now - lastAttach > 500) {
            lastAttach = now;
            viewer.Attach();
        }

        Mode mode = Mode::Waiting;
        bool alive = viewer.Snapshot(st);

        if (alive) {
            if (st.unloadRequested) {
                mode = Mode::Unloaded;
                if (!unloadSeenAt)
                    unloadSeenAt = now;
                if (now - unloadSeenAt > 3000)
                    break;
            } else if (st.menuOpen) {
                mode = Mode::Menu;
                SyncMirrors(st, viewer);
            } else {
                mode = Mode::Hud;
                SyncMirrors(st, viewer);
            }
        }

        // Видимость и кликабельность следуют режиму.
        if (mode == Mode::Hud && !st.hudVisible)
            show = false;
        else
            show = true;
        if (show && !IsWindowVisible(g_hwnd))
            ShowWindow(g_hwnd, SW_SHOWNA);
        if (!show && IsWindowVisible(g_hwnd))
            ShowWindow(g_hwnd, SW_HIDE);

        SetClickThrough(mode != Mode::Menu);

        if (show)
            RenderFrame(viewer, st, mode);

        Sleep(16);
    }

    ShutdownImGuiD3D12();
    for (UINT i = 0; i < kNumFrames; ++i) {
        g_frames[i].fence->SetEventOnCompletion(g_frames[i].fenceVal, g_frames[i].event);
        WaitForSingleObject(g_frames[i].event, 1000);
        CloseHandle(g_frames[i].event);
        g_frames[i].allocator->Release();
        g_frames[i].fence->Release();
    }
    g_cmd->Release();
    g_srvHeap->Release();
    g_rtvHeap->Release();
    g_queue->Release();
    g_sc->Release();
    g_dev->Release();
    DestroyWindow(g_hwnd);
    return 0;
}
