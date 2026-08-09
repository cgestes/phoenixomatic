#include "phoenix_sprite.h"

#include "../ui_colors.h"

namespace phoenix_sprite {

void draw(IGfx& gfx, int x, int y, bool wings_up, float heat) {
  if (heat < 0.0f) heat = 0.0f;
  if (heat > 1.0f) heat = 1.0f;
  // Cool: mostly ember. Hot: the body picks up the highlight colour too.
  IGfxColor body = heat > 0.55f ? COLOR_HOT : COLOR_EMBER;
  IGfxColor high = heat > 0.55f ? COLOR_BRIGHT : COLOR_HOT;

  const char* const* rows = wings_up ? kUp : kDown;
  for (int r = 0; r < kHeight; ++r) {
    const char* line = rows[r];
    for (int c = 0; c < kWidth; ++c) {
      char ch = line[c];
      if (ch == '\0') break;
      switch (ch) {
        case '#': gfx.drawPixel(x + c, y + r, body); break;
        case '*': gfx.drawPixel(x + c, y + r, high); break;
        case '~': gfx.drawPixel(x + c, y + r, COLOR_ALERT); break;
        default: break;
      }
    }
  }
}

}  // namespace phoenix_sprite
