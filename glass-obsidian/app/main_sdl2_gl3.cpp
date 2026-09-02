// =============================================================================
//  Glass Obsidian -- SDL2 + OpenGL 3 (GLSL 130) host for the demo UI
//
//  The portable counterpart of main_win32_dx11.cpp: identical obsidian setup,
//  presented through a core-profile GL context. Works on Linux / macOS /
//  Windows wherever SDL2 does. Contains no game-interaction code.
//
//  Build (see CMakeLists.txt, OBSIDIAN_WITH_BACKENDS=ON):
//    imgui_impl_sdl2.cpp + imgui_impl_opengl3.cpp from the Dear ImGui backends
//    folder are compiled alongside this file and linked with SDL2 + GL.
// =============================================================================

#include "obsidian/obsidian.h"
#include "demo_ui.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <SDL.h>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#else
#include <GL/gl.h>
#endif

#include <cstdio>

namespace obs = obsidian;

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Core profile, GL 3.0+ (the imgui GL3 backend picks "#version 130").
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);   // soft widget edges

    SDL_WindowFlags flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window* window = SDL_CreateWindow("Glass Obsidian",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          1440, 900, flags);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    SDL_GL_SetSwapInterval(1);   // vsync

    // ---- obsidian setup -----------------------------------------------------
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // High-DPI: SDL reports a drawable size larger than the window size on
    // retina / fractional-scaling displays; fold that into the theme scale.
    int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    SDL_GL_GetDrawableSize(window, &fb_w, &fb_h);
    const float dpi_scale = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0f;

    obs::ThemeConfig cfg;
    cfg.ui_scale = dpi_scale;
    obs::LoadFonts(cfg);
    obs::ApplyTheme(obs::PaletteEmber(), cfg);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 130");

    obs::WindowConfig win_cfg;
    win_cfg.title        = "Glass Obsidian";
    win_cfg.initial_size = ImVec2(1180.f, 760.f);
    obs::ObsidianWindow win(win_cfg);

    demo::State st;
    float last_ui_scale = st.ui_scale;
    bool  open          = true;
    bool  done          = false;

    while (!done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) done = true;
            if (ev.type == SDL_WINDOWEVENT &&
                ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                ev.window.windowID == SDL_GetWindowID(window))
                done = true;
        }
        if (done) break;

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        // Live ui_scale slider: the atlas re-bake must happen outside the
        // frame (it is locked between NewFrame and Render), and the GL backend
        // re-uploads the font texture on the next NewFrame automatically.
        if (st.ui_scale != last_ui_scale) {
            last_ui_scale = st.ui_scale;
            cfg.ui_scale  = st.ui_scale * dpi_scale;
            obs::LoadFonts(cfg);
        }

        obs::ApplyTheme(demo::BuildPalette(st), cfg);
        obs::DrawAmbientBackdrop(1.0f);

        if (win.Begin(&open))
            demo::Draw(st, win);
        win.End();
        if (!open) done = true;

        ImGui::Render();

        SDL_GL_GetDrawableSize(window, &fb_w, &fb_h);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
