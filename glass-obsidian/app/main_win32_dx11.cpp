// =============================================================================
//  Glass Obsidian -- Win32 + DirectX 11 host for the demo UI
//
//  This is the "real machine" counterpart of tests/headless_main.cpp: same
//  obsidian::ThemeConfig / LoadFonts / ApplyTheme / ObsidianWindow / demo::Draw
//  calls, but presented through a DX11 swap chain instead of the software
//  rasteriser. It contains no game-interaction code of any kind.
//
//  Build (see CMakeLists.txt, OBSIDIAN_WITH_BACKENDS=ON on Windows):
//    imgui_impl_win32.cpp + imgui_impl_dx11.cpp from the Dear ImGui backends
//    folder are compiled alongside this file and linked with d3d11/dxgi.
// =============================================================================
#if defined(_WIN32)

#include "obsidian/obsidian.h"
#include "demo_ui.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>

namespace obs = obsidian;

// Forward declaration declared by imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

// ---- DX11 globals -----------------------------------------------------------
static ID3D11Device*            g_device        = nullptr;
static ID3D11DeviceContext*     g_device_ctx    = nullptr;
static IDXGISwapChain*          g_swap_chain    = nullptr;
static ID3D11RenderTargetView*  g_rtv           = nullptr;
static UINT                     g_rtv_width     = 0;
static UINT                     g_rtv_height    = 0;

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hWnd;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    UINT create_flags = 0;
    D3D_FEATURE_LEVEL feature_level{};
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0,
    };
    if (S_OK != D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                              create_flags, levels, 2, D3D11_SDK_VERSION,
                                              &sd, &g_swap_chain, &g_device, &feature_level,
                                              &g_device_ctx))
        return false;

    ID3D11Texture2D* back = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return g_rtv != nullptr;
}

static void CleanupDeviceD3D()
{
    if (g_rtv)         { g_rtv->Release();         g_rtv = nullptr; }
    if (g_swap_chain)  { g_swap_chain->Release();  g_swap_chain = nullptr; }
    if (g_device_ctx)  { g_device_ctx->Release();  g_device_ctx = nullptr; }
    if (g_device)      { g_device->Release();      g_device = nullptr; }
}

static void ResizeSwapChain()
{
    if (!g_swap_chain) return;
    ImGui_ImplDX11_InvalidateDeviceObjects();
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_swap_chain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* back = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    g_rtv_width  = desc.Width;
    g_rtv_height = desc.Height;
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    ImGui_ImplDX11_CreateDeviceObjects();
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) ResizeSwapChain();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int show_cmd)
{
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GlassObsidianDemo";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowW(wc.lpszClassName, L"Glass Obsidian",
                              WS_OVERLAPPEDWINDOW, 100, 100, 1440, 900,
                              nullptr, nullptr, hInstance, nullptr);

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }
    ShowWindow(hWnd, show_cmd);
    UpdateWindow(hWnd);

    // ---- obsidian setup -----------------------------------------------------
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Fold the monitor DPI into the theme scale; the demo's own ui_scale
    // slider multiplies on top of it.
    const float dpi_scale = (float)ImGui_ImplWin32_GetDpiForHwnd(hWnd) / 96.0f;
    obs::ThemeConfig cfg;
    cfg.ui_scale = dpi_scale;
    obs::LoadFonts(cfg);
    obs::ApplyTheme(obs::PaletteEmber(), cfg);

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_device, g_device_ctx);

    obs::WindowConfig win_cfg;
    win_cfg.title        = "Glass Obsidian";
    win_cfg.initial_size = ImVec2(1180.f, 760.f);
    obs::ObsidianWindow win(win_cfg);

    demo::State st;
    float last_ui_scale = st.ui_scale;
    bool  open          = true;

    ShowWindow(hWnd, SW_SHOW);
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done) break;

        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();

        // The demo exposes a live ui_scale slider; re-baking the atlas must
        // happen outside NewFrame/Render (the atlas is locked inside a frame)
        // and the DX11 backend's font texture has to be rebuilt afterwards.
        if (st.ui_scale != last_ui_scale) {
            last_ui_scale = st.ui_scale;
            cfg.ui_scale  = st.ui_scale * dpi_scale;
            ImGui_ImplDX11_InvalidateDeviceObjects();
            obs::LoadFonts(cfg);
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        // Styles are derived from the palette every frame (cheap: it is just a
        // ImGuiStyle write), so the Palette tab's opacity / radius sliders take
        // effect immediately.
        obs::ApplyTheme(demo::BuildPalette(st), cfg);

        // Ambient wash behind the translucent window (optional, but it is what
        // makes the glass read as glass).
        obs::DrawAmbientBackdrop(1.0f);

        if (win.Begin(&open))
            demo::Draw(st, win);
        win.End();
        if (!open) done = true;

        ImGui::Render();
        const float clear[4] = { 0.02f, 0.02f, 0.03f, 1.0f };
        g_device_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_device_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap_chain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    UnregisterClassW(wc.lpszClassName, hInstance);
    return 0;
}

#endif // _WIN32
