// ============================================================================
// NEVERWIN — внешний HUD-оверлей.
//
// Не инжектится в игру: рисует собственное окно поверх (topmost, layered,
// click-through) и читает состояние DLL из shared memory (shared_state.hpp).
// Работает при любом рендерере — DX11 и Vulkan. Ограничение: игра должна
// быть в оконном или borderless-режиме; поверх exclusive fullscreen
// внешнее окно не встанет.
//
// Жизненный цикл:
//   ждёт neverwin.dll -> рисует HUD -> после END (выгрузки DLL) показывает
//   "DLL выгружена" и закрывается через 3 секунды.
// Управление HUD: F6 в игре (флаг читается из shared memory).
// ============================================================================
// user32 макросит DrawText/DrawTextEx в DrawTextW — если не снять ДО d2d1.h,
// макрос переименует и сам метод ID2D1RenderTarget::DrawText в заголовке.
#include <windows.h>
#undef DrawText
#undef DrawTextEx

#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "d3d11")

#include <cstdio>
#include <cstring>

#include "shared_state.hpp"

namespace {

    constexpr int   kWidth  = 430;
    constexpr int   kHeight = 330;
    constexpr int   kPosX   = 14;
    constexpr int   kPosY   = 14;
    constexpr UINT8 kAlpha  = 240; // глобальная непрозрачность окна (0-255)

    struct Gfx {
        IDXGISwapChain*       sc    = nullptr;
        ID3D11Device*         dev   = nullptr;
        ID3D11DeviceContext*  ctx   = nullptr;
        ID2D1Factory*         d2d   = nullptr;
        ID2D1RenderTarget*    rt    = nullptr;
        IDWriteFactory*       dw    = nullptr;
        IDWriteTextFormat*    title = nullptr;
        IDWriteTextFormat*    row   = nullptr;
        IDWriteTextFormat*    small = nullptr;
        ID2D1SolidColorBrush* text  = nullptr;
        ID2D1SolidColorBrush* on    = nullptr;
        ID2D1SolidColorBrush* off   = nullptr;
        ID2D1SolidColorBrush* dim   = nullptr;
        ID2D1SolidColorBrush* panel = nullptr;
    } g;

    void ReleaseGfx() {
        const auto rel = [](IUnknown* p) { if (p) p->Release(); };
        rel(g.panel); rel(g.dim); rel(g.off); rel(g.on); rel(g.text);
        rel(g.small); rel(g.row); rel(g.title); rel(g.dw); rel(g.rt); rel(g.d2d);
        rel(g.ctx); rel(g.dev); rel(g.sc);
        g = {};
    }

    bool InitGfx(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count  = 1;
        sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount       = 1;
        sd.OutputWindow      = hwnd;
        sd.Windowed          = TRUE;

        D3D_FEATURE_LEVEL lvl = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &sd, &g.sc, &g.dev, &lvl, &g.ctx)))
            return false;

        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g.d2d)))
            return false;

        IDXGISurface* surface = nullptr;
        if (FAILED(g.sc->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(&surface))))
            return false;

        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
        const HRESULT hrRt = g.d2d->CreateDxgiSurfaceRenderTarget(surface, &props, &g.rt);
        surface->Release();
        if (FAILED(hrRt))
            return false;

        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g.dw))))
            return false;

        g.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 19.0f, L"ru-RU", &g.title);
        g.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ru-RU", &g.row);
        g.dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"ru-RU", &g.small);

        g.rt->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.93f, 0.95f, 1.0f), &g.text);
        g.rt->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.85f, 0.45f, 1.0f), &g.on);
        g.rt->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.42f, 0.40f, 1.0f), &g.off);
        g.rt->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.57f, 0.62f, 1.0f), &g.dim);
        g.rt->CreateSolidColorBrush(D2D1::ColorF(0.055f, 0.06f, 0.08f, 0.90f), &g.panel);
        return true;
    }

    void Text(const wchar_t* s, IDWriteTextFormat* fmt, ID2D1SolidColorBrush* br,
              float x, float y) {
        g.rt->DrawText(s, static_cast<UINT32>(wcslen(s)), fmt,
                       D2D1::RectF(x, y, static_cast<float>(kWidth) - 12.0f,
                                   static_cast<float>(kHeight)),
                       br, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    }

    enum class Mode { Waiting, Hud, Unloaded };

    void RenderFrame(const nwshared::State* st, Mode mode) {
        g.rt->BeginDraw();
        g.rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        g.rt->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                                          static_cast<float>(kHeight)),
                              10.0f, 10.0f),
            g.panel);

        float y = 12.0f;
        Text(L"NEVERWIN", g.title, g.text, 14.0f, y);
        y += 24.0f;
        if (st && st->dllVersion > 0) {
            wchar_t sub[128]{};
            swprintf(sub, 128, L"v%d — professional software for losing professionally",
                     static_cast<int>(st->dllVersion));
            Text(sub, g.small, g.dim, 14.0f, y);
        } else {
            Text(L"professional software for losing professionally", g.small, g.dim, 14.0f, y);
        }
        y += 28.0f;

        if (mode == Mode::Waiting) {
            Text(L"ожидание neverwin.dll...", g.row, g.dim, 14.0f, y);
            Text(L"инжектни DLL — HUD подхватит состояние сам", g.small, g.dim, 14.0f, y + 22.0f);
        } else if (mode == Mode::Unloaded) {
            Text(L"DLL выгружена (END)", g.row, g.off, 14.0f, y);
            Text(L"оверлей закроется через пару секунд", g.small, g.dim, 14.0f, y + 22.0f);
        } else {
            const struct { const wchar_t* key; const wchar_t* name; uint8_t on; } rows[] = {
                { L"[F1]", L"Реверс аимбот: наводка на тимейтов", st->antiAimbot },
                { L"[F2]", L"Антиаимлесс: взгляд в пол",          st->antiAimless },
                { L"[F3]", L"Visual recoil x4",                   st->visualRecoil },
                { L"[F4]", L"Антибхоп",                           st->antiBhop },
                { L"[F5]", L"Gamesense: дроп оружия",             st->gamesense },
            };
            for (const auto& r : rows) {
                Text(r.key, g.row, g.dim, 14.0f, y);
                Text(r.name, g.row, g.text, 62.0f, y);
                Text(r.on ? L"ON" : L"OFF", g.row, r.on ? g.on : g.off,
                     static_cast<float>(kWidth) - 56.0f, y);
                y += 26.0f;
            }
            y += 12.0f;

            wchar_t line[256]{};
            swprintf(line, 256, L"client.dll   0x%llX",
                     static_cast<unsigned long long>(st->clientBase));
            Text(line, g.small, g.dim, 14.0f, y); y += 17.0f;
            swprintf(line, 256, L"LocalPlayer  0x%llX   hp %d   team %d",
                     static_cast<unsigned long long>(st->localPlayer),
                     static_cast<int>(st->localHealth), static_cast<int>(st->localTeam));
            Text(line, g.small, g.dim, 14.0f, y); y += 17.0f;
            Text(st->offsetsFromIni ? L"оффсеты: из neverwin.ini"
                                    : L"оффсеты: ВСТРОЕННЫЕ — обнови ini!",
                 g.small, st->offsetsFromIni ? g.on : g.off, 14.0f, y);
            y += 19.0f;
            Text(L"INSERT - меню | F6 - скрыть HUD | END - выгрузка", g.small, g.dim, 14.0f, y);
        }

        g.rt->EndDraw();
    }

    bool IsOwnerAlive(uint32_t pid) {
        if (!pid)
            return false;
        HANDLE ph = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!ph)
            return false;
        DWORD code = 0;
        GetExitCodeProcess(ph, &code);
        CloseHandle(ph);
        return code == STILL_ACTIVE;
    }

} // namespace

int wmain() {
    SetProcessDPIAware();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NeverwinOverlayWnd";
    RegisterClassExW(&wc);

    const HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"neverwin overlay", WS_POPUP,
        kPosX, kPosY, kWidth, kHeight,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
        return 1;

    SetLayeredWindowAttributes(hwnd, 0, kAlpha, LWA_ALPHA);

    if (!InitGfx(hwnd)) {
        MessageBoxW(nullptr, L"не смог поднять Direct2D", L"neverwin overlay", MB_ICONERROR);
        return 1;
    }

    nwshared::Viewer viewer;
    nwshared::State  st{};
    ULONGLONG lastAttach = 0, lastOwnerCheck = 0, unloadSeenAt = 0;
    bool ownerOk = false;

    ShowWindow(hwnd, SW_SHOWNA);

    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        const ULONGLONG now = GetTickCount64();

        if (!viewer.Alive() && now - lastAttach > 500) {
            lastAttach = now;
            viewer.Attach();
        }

        if (viewer.Snapshot(st)) {
            if (now - lastOwnerCheck > 500) {
                lastOwnerCheck = now;
                ownerOk = IsOwnerAlive(st.ownerPid);
            }

            if (ownerOk && !st.unloadRequested) {
                unloadSeenAt = 0;
                if (!st.hudVisible) {
                    if (IsWindowVisible(hwnd))
                        ShowWindow(hwnd, SW_HIDE);
                } else {
                    if (!IsWindowVisible(hwnd))
                        ShowWindow(hwnd, SW_SHOWNA);
                    RenderFrame(&st, Mode::Hud);
                    g.sc->Present(0, 0);
                }
            } else if (st.unloadRequested || !ownerOk) {
                ShowWindow(hwnd, SW_SHOWNA);
                RenderFrame(&st, Mode::Unloaded);
                g.sc->Present(0, 0);
                if (!unloadSeenAt)
                    unloadSeenAt = now;
                if (now - unloadSeenAt > 3000)
                    break; // DLL выгрузилась — оверлею больше нечего показывать
            } else {
                if (!IsWindowVisible(hwnd))
                    ShowWindow(hwnd, SW_SHOWNA);
                RenderFrame(&st, Mode::Waiting);
                g.sc->Present(0, 0);
            }
        } else {
            if (!IsWindowVisible(hwnd))
                ShowWindow(hwnd, SW_SHOWNA);
            RenderFrame(nullptr, Mode::Waiting);
            g.sc->Present(0, 0);
        }

        Sleep(16);
    }

    ReleaseGfx();
    DestroyWindow(hwnd);
    return 0;
}
