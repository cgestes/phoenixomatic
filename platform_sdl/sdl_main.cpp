// Desktop and web entry point. The same UI the Cardputer runs, in a window.
#include <cstdio>
#include <cstdlib>

#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "../src/core/model.h"
#include "../src/ui/phoenix_display.h"
#include "sdl_display.h"

namespace {

constexpr int kPanelW = 240;
constexpr int kPanelH = 135;
constexpr int kDefaultScale = 4;

struct App {
  SDLDisplay* gfx = nullptr;
  PhoenixModel* model = nullptr;
  PhoenixDisplay* ui = nullptr;
  uint32_t last_ticks = 0;
  bool running = true;
};

App g_app;

// Maps an SDL key event onto the Cardputer's much smaller keyboard.
bool translate(const SDL_KeyboardEvent& key, UIEvent& out) {
  SDL_Keymod mod = SDL_GetModState();
  out.ctrl = (mod & (KMOD_CTRL | KMOD_GUI)) != 0;
  out.shift = (mod & KMOD_SHIFT) != 0;
  out.alt = (mod & KMOD_ALT) != 0;

  switch (key.keysym.sym) {
    case SDLK_UP:        out.code = KEY_UP; return true;
    case SDLK_DOWN:      out.code = KEY_DOWN; return true;
    case SDLK_LEFT:      out.code = KEY_LEFT; return true;
    case SDLK_RIGHT:     out.code = KEY_RIGHT; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  out.code = KEY_ENTER; return true;
    case SDLK_ESCAPE:    out.code = KEY_ESC; return true;
    case SDLK_TAB:       out.code = KEY_TAB; return true;
    case SDLK_BACKSPACE: out.code = KEY_BACKSPACE; return true;
    default: break;
  }

  SDL_Keycode sym = key.keysym.sym;
  if (sym >= 0x20 && sym < 0x7F) {
    out.key = static_cast<char>(sym);  // SDL already reports these lowercase
    return true;
  }
  return false;
}

void frame() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {
      g_app.running = false;
    } else if (e.type == SDL_WINDOWEVENT) {
      switch (e.window.event) {
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:
          g_app.ui->invalidate();
          break;
        default:
          break;
      }
    } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      UIEvent ui;
      if (translate(e.key, ui)) g_app.ui->handleKey(ui);
    } else if (e.type == SDL_KEYDOWN && e.key.repeat) {
      // Held arrows should keep sweeping an attenuverter.
      UIEvent ui;
      if (translate(e.key, ui) && ui.code != KEY_NONE) g_app.ui->handleKey(ui);
    }
  }

  uint32_t now = SDL_GetTicks();
  float dt = static_cast<float>(now - g_app.last_ticks) / 1000.0f;
  g_app.last_ticks = now;

  // Deliberately no clear() here: TextScreen paints every cell's background
  // and repaints only what changed, so wiping the panel each frame would erase
  // every cell that happened to stay still.
  g_app.ui->update(dt);
  g_app.gfx->flush();
}

#ifdef __EMSCRIPTEN__
void emFrame() {
  frame();
  if (!g_app.running) emscripten_cancel_main_loop();
}
#endif

}  // namespace

// `phoenixomatic shot <dir>` walks every page and sub-page and writes a BMP of
// each, so the whole UI can be reviewed without clicking through it.
int captureAll(SDLDisplay& gfx, PhoenixModel& model, PhoenixDisplay& ui,
               const char* dir) {
  ui.dismissSplash();
  // Let the fake state settle so the screens aren't all at zero.
  for (int i = 0; i < 90; ++i) ui.update(1.0f / 30.0f);

  char path[512];
  int shots = 0;
  const int page_count = 9;
  for (int p = 0; p < page_count; ++p) {
    while (ui.pageIndex() != p) ui.nextPage();
    // Sub-page count is private to the page, so just try a few and stop when
    // the header stops changing.
    for (int s = 0; s < 3; ++s) {
      UIEvent ev;
      ev.code = KEY_DOWN;
      ev.ctrl = true;
      if (s > 0) ui.handleKey(ev);
      for (int i = 0; i < 4; ++i) ui.update(1.0f / 30.0f);
      snprintf(path, sizeof(path), "%s/page%d_%d.bmp", dir, p + 1, s + 1);
      if (gfx.saveBmp(path)) ++shots;
    }
  }
  snprintf(path, sizeof(path), "%s/splash.bmp", dir);
  (void)model;
  printf("phoenixomatic: wrote %d screenshots to %s\n", shots, dir);
  return 0;
}

int main(int argc, char** argv) {
  int scale = kDefaultScale;
  bool shot_mode = argc > 2 && SDL_strcmp(argv[1], "shot") == 0;
  if (shot_mode) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    scale = 1;
  } else if (argc > 1) {
    int s = atoi(argv[1]);
    if (s >= 1 && s <= 8) scale = s;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    fprintf(stderr, "phoenixomatic: SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDLDisplay gfx(kPanelW, kPanelH, scale, "phoenixomatic \xC2\xB7 modularcore");
  gfx.begin();
  if (!gfx.ok()) {
    SDL_Quit();
    return 1;
  }

  PhoenixModel model;
  PhoenixDisplay ui(gfx, model);

  g_app.gfx = &gfx;
  g_app.model = &model;
  g_app.ui = &ui;
  g_app.last_ticks = SDL_GetTicks();

  if (shot_mode) {
    int rc = captureAll(gfx, model, ui, argv[2]);
    SDL_Quit();
    return rc;
  }

#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop(emFrame, 0, 1);
#else
  while (g_app.running) {
    frame();
    SDL_Delay(16);
  }
#endif

  SDL_Quit();
  return 0;
}
