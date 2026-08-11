// phoenixomatic — M5Stack Cardputer ADV entry point.
//
// Same UI and engine code as the desktop and web builds; only the display
// backend, the keyboard translation and the audio task live here.
#include <M5Cardputer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "cardputer_display.h"
#include "src/core/model.h"
#include "src/dsp/audio_config.h"
#include "src/dsp/phoenix_engine.h"
#include "src/ui/phoenix_display.h"

namespace {

CardputerDisplay g_display;
PhoenixModel g_model;
PhoenixDisplay* g_ui = nullptr;
PhoenixEngine* g_engine = nullptr;
TaskHandle_t g_audio_task = nullptr;
int16_t g_audio_buf[kBlockSize * 2];   // interleaved stereo

unsigned long g_last_ms = 0;

// The Cardputer puts the arrows on ; . , / — these are their HID codes.
constexpr uint8_t kHidUp = 0x33;
constexpr uint8_t kHidDown = 0x37;
constexpr uint8_t kHidLeft = 0x36;
constexpr uint8_t kHidRight = 0x38;
constexpr uint8_t kHidEnter = 0x28;
constexpr uint8_t kHidEnter2 = 0x58;
constexpr uint8_t kHidA = 0x04;
constexpr uint8_t kHidZ = 0x1D;

constexpr unsigned long kRepeatDelayMs = 320;
constexpr unsigned long kRepeatRateMs = 55;

void dispatch(const UIEvent& ev) {
  if (g_ui) g_ui->handleKey(ev);
}

// Returns true if anything was sent, so the caller knows to arm key repeat.
bool processKeys(const Keyboard_Class::KeysState& ks) {
  bool sent = false;

  for (auto hid : ks.hid_keys) {
    UIEvent ev;
    ev.ctrl = ks.ctrl;
    ev.shift = ks.shift;
    ev.alt = ks.alt;
    bool send = true;
    switch (hid) {
      case kHidUp:     ev.code = KEY_UP; break;
      case kHidDown:   ev.code = KEY_DOWN; break;
      case kHidLeft:   ev.code = KEY_LEFT; break;
      case kHidRight:  ev.code = KEY_RIGHT; break;
      case kHidEnter:
      case kHidEnter2: ev.code = KEY_ENTER; break;
      default:
        // Modified letters still need to reach the UI, since the character
        // stream suppresses them.
        if ((ks.ctrl || ks.alt) && hid >= kHidA && hid <= kHidZ) {
          ev.key = static_cast<char>('a' + (hid - kHidA));
        } else {
          send = false;
        }
        break;
    }
    if (send) {
      dispatch(ev);
      sent = true;
    }
  }

  if (ks.tab) {
    UIEvent ev;
    ev.code = KEY_TAB;
    ev.shift = ks.shift;
    dispatch(ev);
    sent = true;
  }
  if (ks.del) {
    UIEvent ev;
    ev.code = KEY_BACKSPACE;
    dispatch(ev);
    sent = true;
  }

  // Plain characters. Skipped when a modifier is held so a modified key does
  // not arrive twice.
  if (!ks.ctrl && !ks.alt) {
    for (auto c : ks.word) {
      UIEvent ev;
      ev.shift = ks.shift;
      ev.key = c;
      if (c == '`' || c == '~') ev.code = KEY_ESC;
      dispatch(ev);
      sent = true;
    }
  }
  return sent;
}

void pollKeyboard() {
  static Keyboard_Class::KeysState held{};
  static bool has_held = false;
  static unsigned long next_repeat = 0;

  M5Cardputer.update();
  bool changed = M5Cardputer.Keyboard.isChange();
  bool pressed = M5Cardputer.Keyboard.isPressed();

  if (changed && pressed) {
    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
    if (processKeys(ks)) {
      held = ks;
      has_held = true;
      next_repeat = millis() + kRepeatDelayMs;
    }
  } else if (!pressed) {
    has_held = false;
  } else if (has_held && millis() >= next_repeat) {
    // Holding an arrow should keep sweeping an attenuverter.
    processKeys(held);
    next_repeat = millis() + kRepeatRateMs;
  }
}

// Audio runs on its own core so a slow panel repaint cannot stall the sound.
void audioTask(void*) {
  for (;;) {
    while (M5Cardputer.Speaker.isPlaying()) {
      vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    if (g_engine) {
      g_engine->render(g_audio_buf, kBlockSize);
      // playRaw counts samples, not frames, and the last flag is stereo.
      M5Cardputer.Speaker.playRaw(g_audio_buf, kBlockSize * 2, kSampleRate, true);
    } else {
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
  }
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);

  g_display.begin();
  g_display.clear(IGfxColor(0x0A0B0D));

  g_ui = new PhoenixDisplay(g_display, g_model);
  g_engine = new PhoenixEngine(g_model, static_cast<float>(kSampleRate));

  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(160);
  xTaskCreatePinnedToCore(audioTask, "phx_audio", 8192, nullptr, 2,
                          &g_audio_task, 0);

  g_last_ms = millis();
}

void loop() {
  pollKeyboard();

  unsigned long now = millis();
  // ~25 fps is plenty for a 240x135 panel and leaves headroom for the DSP.
  if (now - g_last_ms < 40) {
    delay(2);
    return;
  }
  float dt = static_cast<float>(now - g_last_ms) / 1000.0f;
  g_last_ms = now;

  if (g_ui) g_ui->update(dt);
}
