// Desktop and web entry point. The same UI the Cardputer runs, in a window.
#include <cmath>
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
#include "../src/dsp/audio_config.h"
#include "../src/dsp/phoenix_engine.h"
#include "../src/ui/phoenix_display.h"
#include "mac_menu.h"
#include "sdl_audio.h"
#include "sdl_display.h"

namespace {

constexpr int kPanelW = 240;
constexpr int kPanelH = 135;
constexpr int kDefaultScale = 4;

struct App {
  SDLDisplay* gfx = nullptr;
  PhoenixModel* model = nullptr;
  PhoenixDisplay* ui = nullptr;
  PhoenixEngine* engine = nullptr;
  uint32_t last_ticks = 0;
  bool running = true;
  bool dragging = false;
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
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
      // Window pixels to panel pixels. Emscripten already maps the canvas's
      // CSS size back to its internal resolution, so the same divide works on
      // the web.
      int s = g_app.gfx->scale();
      g_app.ui->mouseDown(e.button.x / s, e.button.y / s);
      g_app.dragging = true;
    } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
      g_app.dragging = false;
    } else if (e.type == SDL_MOUSEMOTION && g_app.dragging) {
      bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
      g_app.ui->mouseDrag(e.motion.yrel, shift);
    } else if (e.type == SDL_MOUSEWHEEL) {
      bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
      g_app.ui->mouseWheel(e.wheel.y, shift);
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
               PhoenixEngine& engine, const char* dir) {
  ui.dismissSplash();

  // The engine is the only thing that moves state now, so drive it directly
  // instead of waiting on an audio device. One second of audio, and a peak /
  // RMS report so "it renders" and "it makes a sound" are separate claims.
  int16_t buf[kBlockSize * kChannels];
  double sum_sq = 0.0, diff_sq = 0.0;
  int peak = 0;
  size_t total = 0;
  for (int b = 0; b < static_cast<int>(engine.sampleRate()) /
                          static_cast<int>(kBlockSize);
       ++b) {
    engine.render(buf, kBlockSize);
    for (size_t i = 0; i < kBlockSize; ++i) {
      int l = buf[i * kChannels], r = buf[i * kChannels + 1];
      int a = l < 0 ? -l : l, c = r < 0 ? -r : r;
      if (a > peak) peak = a;
      if (c > peak) peak = c;
      sum_sq += (static_cast<double>(l) * l + static_cast<double>(r) * r) * 0.5;
      // How far apart the channels are, so "stereo" is a measurement and not
      // a claim about the buffer layout.
      diff_sq += static_cast<double>(l - r) * (l - r);
      ++total;
    }
  }
  double rms = total ? std::sqrt(sum_sq / static_cast<double>(total)) : 0.0;
  double width = total ? std::sqrt(diff_sq / static_cast<double>(total)) : 0.0;
  printf("phoenixomatic: 1s render — peak %d (%.1f%% FS), rms %.0f, "
         "L-R %.0f, comp %.0f Hz\n",
         peak, 100.0 * peak / 32767.0, rms, width,
         static_cast<double>(model.comp_hz));

  for (int i = 0; i < 4; ++i) ui.update(1.0f / 30.0f);

  char path[512];
  int shots = 0;
  // Walk the flat screen list rather than hunting for page indices: a mode can
  // hide a page entirely, and looking for one that is not in the walk spins
  // forever.
  int screens = ui.screenCount();
  for (int i = 0; i < screens; ++i) {
    // Interleave audio and UI frames: pages with a history overlay sample it
    // once per draw, so a screenshot taken after a single render shows an
    // empty plot rather than the machine running.
    // ~25 s of machine time per screen, so history overlays have something to
    // show. Cheap: the engine renders far faster than real time.
    for (int f = 0; f < 750; ++f) {
      engine.render(buf, kBlockSize);
      ui.update(1.0f / 30.0f);
    }
    snprintf(path, sizeof(path), "%s/screen%02d.bmp", dir, i + 1);
    if (gfx.saveBmp(path)) ++shots;
    ui.nextPage();
  }

  snprintf(path, sizeof(path), "%s/splash.bmp", dir);
  (void)model;
  printf("phoenixomatic: wrote %d screenshots to %s\n", shots, dir);
  return 0;
}

int main(int argc, char** argv) {
  int scale = kDefaultScale;
  bool shot_mode = argc > 2 && SDL_strcmp(argv[1], "shot") == 0;
  // --audio-list prints the outputs and --audio N picks one, for when the
  // device you want is not the system default and you would rather not click.
  bool list_devices = false;
  int device_index = -1;
  int rate_hz = kSampleRate;
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (SDL_strcmp(a, "-h") == 0 || SDL_strcmp(a, "--help") == 0) {
      printf(
          "phoenixomatic - a benjolin-flavoured chaos machine\n"
          "\n"
          "usage: phoenixomatic [scale] [options]\n"
          "       phoenixomatic shot <dir>\n"
          "\n"
          "  scale            window size, 1..8 (default %d)\n"
          "  shot <dir>       write a BMP of every screen and exit\n"
          "  --audio-list     list the audio outputs and exit\n"
          "  --audio <n>      start on output n (-1 is the system default)\n"
          "  -h, --help       this\n"
          "\n"
          "keys:\n"
          "  up/down          move between rows\n"
          "  left/right       move between the fields of a row\n"
          "  a/z s/x d/c ...  raise or lower fields 1..8, one keyboard column\n"
          "                   each; hold shift for the fine step\n"
          "  o / shift+o      zero the field / the page\n"
          "  r / t / shift+r  randomise the field / the row / the page\n"
          "  space            toggle what is focused (play on HOME)\n"
          "  [ ]              previous / next screen\n"
          "  ctrl+up/down     sub-page\n"
          "  1-7              mute an instrument\n"
          "  - = esc          mute all / unmute all / invert\n"
          "\n"
          "  --rate <hz>      8000 11025 16000 22050 32000 44100 48000\n"
          "\n"
          "mouse: click a value, then drag or scroll. one notch or ten pixels\n"
          "       is one keypress. on macOS the menu bar picks the output and\n"
          "       the rate.\n",
          kDefaultScale);
      return 0;
    }
    if (SDL_strcmp(a, "--audio-list") == 0) list_devices = true;
    else if (SDL_strcmp(a, "--audio") == 0 && i + 1 < argc) {
      device_index = atoi(argv[++i]);
    } else if (SDL_strcmp(a, "--rate") == 0 && i + 1 < argc) {
      rate_hz = atoi(argv[++i]);
      if (rate_hz < 4000 || rate_hz > kMaxSampleRate) {
        fprintf(stderr,
                "phoenixomatic: --rate %d is outside 4000..%d, using %d\n",
                rate_hz, kMaxSampleRate, kSampleRate);
        rate_hz = kSampleRate;
      }
    }
  }
  if (shot_mode) {
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    scale = 1;
  } else if (argc > 1) {
    int s = atoi(argv[1]);
    if (s >= 1 && s <= 8) scale = s;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "phoenixomatic: SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // Informational: print the outputs and stop, rather than starting the
  // machine so the list can scroll past.
  if (list_devices) {
    int n = SDL_GetNumAudioDevices(0);
    printf("audio outputs:\n  -1  (system default)\n");
    for (int i = 0; i < n; ++i) printf("  %2d  %s\n", i, SDL_GetAudioDeviceName(i, 0));
    SDL_Quit();
    return 0;
  }

  SDLDisplay gfx(kPanelW, kPanelH, scale, "phoenixomatic \xC2\xB7 modularcore");
  gfx.begin();
  if (!gfx.ok()) {
    SDL_Quit();
    return 1;
  }

  PhoenixModel model;
  PhoenixDisplay ui(gfx, model);
  PhoenixEngine engine(model, static_cast<float>(rate_hz));

  // Screenshot mode stays silent — it runs faster than real time.
  SdlAudio audio;
  if (!shot_mode) {
    if (!audio.start(&engine, rate_hz)) {
      fprintf(stderr, "phoenixomatic: no audio (%s) — running silent\n", SDL_GetError());
    }
    // The pickers live in the menu bar rather than on a page: they are
    // properties of this machine, not of the instrument, and the Cardputer has
    // no choice to offer on either. Installed even with one device, because
    // the rate is always worth choosing.
    installAudioMenu(&audio);
    if (device_index >= 0 && !audio.select(device_index)) {
      fprintf(stderr, "phoenixomatic: device %d not available\n", device_index);
    }
    if (audio.rate() != rate_hz) {
      fprintf(stderr, "phoenixomatic: %d Hz refused, running at %d\n", rate_hz,
              audio.rate());
    }
  }

  g_app.engine = &engine;
  g_app.gfx = &gfx;
  g_app.model = &model;
  g_app.ui = &ui;
  g_app.last_ticks = SDL_GetTicks();

  if (shot_mode) {
    int rc = captureAll(gfx, model, ui, engine, argv[2]);
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

  audio.stop();
  SDL_Quit();
  return 0;
}
