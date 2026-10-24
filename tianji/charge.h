// Hold-to-charge throwing: hold Space/Enter (or shake the device) to build power, release to throw.
#pragma once
#include "gfx.h"
#include "input.h"

struct Charge {
  bool active = false, viaShake = false;
  float level = 0, quiet = 0, t = 0;
  bool tapped = false;
  bool armed = false;  // must see the keys up once, so the press that opened the screen doesn't throw

  void reset() {
    active = viaShake = tapped = armed = false;
    level = quiet = t = 0;
  }

  // Call from key(): a press that is released before the next frame still throws.
  void tap() { tapped = true; }

  // Returns the throw strength (0.3..1) on the frame the throw happens, 0 otherwise.
  // `holdT` reports how long the current charge has been held, for wobble effects.
  float update(float dt, bool canStart) {
    bool held = keyDown(' ') || keyDown(K_ENTER);
    float sh = shakeLevel();
    if (!armed) {
      armed = !held && sh < 0.3f;
      tapped = false;
      return 0;
    }
    if (!active) {
      if (!canStart) {
        tapped = false;
        return 0;
      }
      if (held || tapped || sh > 0.5f) {
        active = true;
        viaShake = !held && !tapped && sh > 0.5f;
        level = 0;
        quiet = 0;
        t = 0;
        if (tapped && !held) {
          tapped = false;
          active = false;
          return 0.35f;
        }
        tapped = false;
      }
      return 0;
    }
    t += dt;
    if (viaShake) {
      level = std::max(level, clamp01((sh - 0.3f) / 1.6f));
      level = std::min(1.0f, level + dt * 0.25f);
      quiet = sh < 0.3f ? quiet + dt : 0;
      if (quiet > 0.22f) return release();
    } else {
      level = std::min(1.0f, level + dt / 1.1f);
      if (!held) return release();
    }
    return 0;
  }

  float release() {
    active = false;
    return 0.3f + 0.7f * easeOutCubic(level);
  }

  // A slim power gauge along the bottom edge.
  void draw(float time) {
    if (!active) return;
    int w = 96, x = SW / 2 - w / 2, y = SH - 7;
    float e = easeOutCubic(level);
    int fw = (int)(w * e);
    cv.fillRoundRect(x, y, w, 3, 1, col::INK3);
    for (int i = 0; i < fw; ++i) cv.drawFastVLine(x + i, y, 3, blend565(col::GOLD2, i > w * 0.8f ? col::EMBER : col::GOLD, (float)i / w));
    float pulse = level >= 1 ? 0.5f + 0.5f * sinf(time * 14) : 0.4f;
    glowDot(x + fw, y + 1, 6 + e * 6, level >= 1 ? col::EMBER : col::GOLD, 0.3f + 0.3f * pulse);
  }
};
