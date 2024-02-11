// Dear ImGui: standalone example application for SDL2 + OpenGL
// (SDL is a cross-platform general purpose library for handling windows,
// inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/
// folder).
// - Introduction, links and more at the top of imgui.cpp

#include <SDL.h>
#include <stdio.h>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <thread>
#include "datastructures/editorconfig.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/imgui.h"
#include "lib.h"

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

// Main code
namespace chr = std::chrono;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
  // Setup SDL
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) !=
      0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100
  const char* glsl_version = "#version 100";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
  // GL 3.2 Core + GLSL 150
  const char* glsl_version = "#version 150";
  SDL_GL_SetAttribute(
      SDL_GL_CONTEXT_FLAGS,
      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // Always required on Mac
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  // GL 3.0 + GLSL 130
  const char* glsl_version = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

  // From 2.0.18: Enable native IME.
#ifdef SDL_HINT_IME_SHOW_UI
  SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

  // Create window with graphics context
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                        SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Window* window =
      SDL_CreateWindow("elide", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                       1280, 720, window_flags);
  if (window == nullptr) {
    printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
    return -1;
  }

  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1);  // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Load Fonts
  // - If no fonts are loaded, dear imgui will use the default font. You can
  // also load multiple fonts and use ImGui::PushFont()/PopFont() to select
  // them.
  // - AddFontFromFileTTF() will return the ImFont* so you can store it if you
  // need to select the font among multiple.
  // - If the file cannot be loaded, the function will return a nullptr. Please
  // handle those errors in your application (e.g. use an assertion, or display
  // an error and quit).
  // - The fonts will be rasterized at a given size (w/ oversampling) and stored
  // into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which
  // ImGui_ImplXXXX_NewFrame below will call.
  // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype
  // for higher quality font rendering.
  // - Read 'docs/FONTS.md' for more instructions and details.
  // - Remember that in C/C++ if you want to include a backslash \ in a string
  // literal you need to write a double backslash \\ !
  // - Our Emscripten build process allows embedding fonts to be accessible at
  // runtime from the "fonts/" folder. See Makefile.emscripten for details.
  // io.Fonts->AddFontDefault();
  // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
  // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
  // ImFont* font =
  const fs::path exe_path = get_executable_path();
  const fs::path exe_folder = exe_path.parent_path();
  // const fs::path fontpath = exe_folder / "mononoki-Regular.ttf";
  const fs::path fontpath = exe_folder / "Meslo LG S DZ Regular for Powerline.ttf";
  ImFontConfig config;
  config.MergeMode = false;
 
  static const ImWchar ranges[] = {
      // 0x0000, 0xFFFE, // basic multilingual plane
      0x2190, 0x21FF, // arrows
      0x0020, 0x00FF,  // Basic Latin + Latin Supplement
      0x0020, 0x007F,  // Basic Latin
      0x00A0, 0x00FF,  // Latin-1 Supplement
      0x0370, 0x03FF,  // Greek and Coptic
      0x2200, 0x22FF,  // Mathematical Operators
      0x2600, 0x26FF,  // Miscellaneous Symbols
      0x2A00, 0x2AFF,  // Supplemental Mathematical Operators
      0x2100, 0x214F,  // Letterlike Symbols
      0,               // End of ranges
  };
  config.GlyphRanges = ranges;

  io.Fonts->GetGlyphRangesDefault();
  ImFont* font =
      io.Fonts->AddFontFromFileTTF(fontpath.string().c_str(), 15.0f, &config);
  io.FontDefault = font;
  io.Fonts->Build();

  initEditor();

  if (argc >= 2) {
    g_editor.original_cwd = fs::canonical(fs::path(argv[1]));
  } else {
    g_editor.original_cwd = fs::absolute(fs::current_path());
  }

  if (fs::is_regular_file(g_editor.original_cwd)) {
    const fs::path filepath = g_editor.original_cwd;
    g_editor.original_cwd = g_editor.original_cwd.remove_filename();
    g_editor.original_cwd = ctrlpGetGoodRootDirAbsolute(g_editor.original_cwd);
    g_editor.getOrOpenNewFile(FileLocation(filepath, Cursor(0, 0)));
  } else {
    g_editor.original_cwd = ctrlpGetGoodRootDirAbsolute(g_editor.original_cwd);
    ctrlpOpen(&g_editor.ctrlp, VM_NORMAL, g_editor.original_cwd);
  }

  tilde::tildeWrite("original_cwd: '%s'", g_editor.original_cwd.c_str());
  tilde::tildeWrite(u8"builtin_initialize baseTypeExt : EnvExtension BaseTypeExtState ← β ℕ ∀");

  // Main loop
  bool done = false;
  while (!done) {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to
    // tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to
    // your main application, or clear/overwrite your copy of the mouse data.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input
    // data to your main application, or clear/overwrite your copy of the
    // keyboard data. Generally you may always pass all inputs to dear imgui,
    // and hide them from your application based on those two flags.
    auto tbegin = Debouncer::get_time();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        done = true;
      }
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(window)) {
        done = true;
      }
    }

    editorProcessKeypress(event);
    editorTickPostKeypress();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // 3. Show another simple window.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("~ Debug Log", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    for (std::string& s : tilde::g_tilde.log) {
      ImGui::Text(s.c_str());
    }
    ImGui::End();

    // editorDraw();

    // Rendering
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);

    ImVec4 clear_color(0.5, 0.5, 0.5, 1);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);

    auto tend = Debouncer::get_time();
    chr::nanoseconds elapsed_nsec = tend - tbegin;
    auto elapsed_sec = chr::duration_cast<chr::seconds>(elapsed_nsec);
    if (elapsed_sec.count() > 0) {
      continue;
    }

    auto elapsed_microsec = chr::duration_cast<chr::microseconds>(elapsed_nsec);
    const chr::microseconds total_microsec(
        1000000 /
        30);  // 30 FPS = 1s / 30 frames = 1000000 microsec / 120 frames
    const auto sleep_microsec = total_microsec - elapsed_microsec;
    std::this_thread::sleep_for(sleep_microsec);
  }

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
/*
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <iterator>
#include <thread>
#include "datastructures/editorconfig.h"
#include "lib.h"

namespace chr = std::chrono;
int main(int argc, char **argv){
  // make stdin non blocking.
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

  enableRawMode();
  initEditor();

  disableRawMode();

  if (argc >= 2) { g_editor.original_cwd = fs::path(argv[1]); }

  if (argc >= 2) {
    g_editor.original_cwd = fs::canonical(fs::path(argv[1]));
  } else {
    g_editor.original_cwd = fs::absolute(fs::current_path());
  }

  if (fs::is_regular_file(g_editor.original_cwd)) {
    const fs::path filepath = g_editor.original_cwd;
    g_editor.original_cwd = g_editor.original_cwd.remove_filename();
    g_editor.original_cwd = ctrlpGetGoodRootDirAbsolute(g_editor.original_cwd);
    g_editor.getOrOpenNewFile(FileLocation(filepath, Cursor(0, 0)));
    g_editor.getOrOpenNewFile(FileLocation(filepath, Cursor(0, 0)));
  } else {
    g_editor.original_cwd = ctrlpGetGoodRootDirAbsolute(g_editor.original_cwd);
    ctrlpOpen(&g_editor.ctrlp, VM_NORMAL, g_editor.original_cwd);
  }

  tilde::tildeWrite("original_cwd: '%s'", g_editor.original_cwd.c_str());

  enableRawMode();
  while (1) {
    auto tbegin = Debouncer::get_time();

    editorDraw();
    editorProcessKeypress();
    editorTickPostKeypress();

    auto tend = Debouncer::get_time();
    chr::nanoseconds elapsed_nsec = tend - tbegin;
    auto elapsed_sec = chr::duration_cast<chr::seconds>(elapsed_nsec);
    if (elapsed_sec.count() > 0) { continue; }

    auto elapsed_microsec = chr::duration_cast<chr::microseconds>(elapsed_nsec);
    const chr::microseconds total_microsec(1000000 / 120); // 120 FPS = 1s / 120
frames = 1000000 microsec / 120 frames
    std::this_thread::sleep_for(total_microsec - elapsed_microsec);

  };
  disableRawMode();
  return 0;
}
*/
