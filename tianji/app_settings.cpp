// 设置: a sliding list. ←/→ change values, Enter edits text or runs an action.
#include <M5Cardputer.h>
#include "app.h"
#include "rec.h"
#include "sfx.h"
#include "system.h"
#include "kclick.h"
#include "pomo.h"
#include "standby.h"
#include "timepick.h"

namespace {

enum Row {
  R_BIRTH,
  R_LIFE,
  R_CLOCK,
  R_SSID,
  R_PASS,
  R_SYNC,
  R_NTPMODE,
  R_WIFIOFF,
  R_TZ,
  R_BRIGHT,
  R_VOL,
  R_SFX,
  R_RATE,
  R_GAIN,
  R_RECDIM,
  R_RECOFF,
  R_POMOOFF,
  R_POMOAUTO,
  R_BLE,
  R_USBAUTO,
  R_USB,
  R_STANDBY,
  R_ABOUT,
  R_N
};

const char* kNames[R_N] = {"生日",   "预期寿命", "日期时间", "WiFi 名称", "WiFi 密码", "联网校时", "自动校时", "校时后关WiFi",
                           "时区",   "亮度",     "音量",     "音效",      "录音采样率", "录音增益",
                           "录音调暗", "录音熄屏", "专注熄屏", "番茄钟自动开始", "蓝牙翻页器", "插电自动U盘", "U盘模式", "进入待机", "关于"};

constexpr int RH = 18;

class Settings_ : public App {
 public:
  void enter() override {
    editing = -1;
    dirty = false;
  }
  void leave() override {
    if (dirty) settingsSave();
    inputTextMode(false);
  }

  bool key(const KeyEvent& e) override {
    if (picker.key(e)) return true;
    if (editing >= 0) return keyEdit(e);
    if (e.key == K_UP) sel = (sel + R_N - 1) % R_N, sfxTick(1.2f, 0.3f);
    else if (e.key == K_DOWN) sel = (sel + 1) % R_N, sfxTick(1.2f, 0.3f);
    else if (e.key == K_LEFT || e.key == K_RIGHT) adjust(e.key == K_RIGHT ? 1 : -1);
    else if (e.key == K_ENTER || e.key == ' ') activate();
    else return false;
    return true;
  }

  void frame(float dt) override {
    t += dt;
    if (restartAt && millis() > restartAt) ESP.restart();
    cv.fillSprite(col::INK);
    text("设置", 6, 2, &F12, col::DIM);
    int top = 20, vis = (SH - top) / RH;
    float want = std::max(0, std::min(sel - vis / 2, R_N - vis));
    scroll = approach(scroll, want, 14, dt);
    hi = approach(hi, sel, 22, dt);
    int hy = top + (int)((hi - scroll) * RH);
    roundPanel(4, hy, SW - 8, RH - 1, 5, blend565(col::INK, col::GOLD, 0.1f), blend565(col::INK, col::GOLD, 0.5f));
    for (int i = (int)scroll; i < R_N && i <= (int)scroll + vis; ++i) {
      int y = top + (int)((i - scroll) * RH);
      bool on = i == sel;
      text(kNames[i], 12, y + 1, &F12, on ? col::BONE : col::DIM);
      drawValue(i, y, on);
    }
    if (editing >= 0) drawEditor();
    picker.draw(dt);
  }

 private:
  int sel = 0, editing = -1;
  float t = 0, scroll = 0, hi = 0;
  bool dirty = false, fresh = false;
  uint32_t restartAt = 0;
  char buf[66];
  char err[32] = "";
  TimePicker picker;

  void bar(int y, float v, bool on) {
    int x = SW - 70, w = 58;
    cv.fillRoundRect(x, y + 7, w, 3, 1, col::INK3);
    cv.fillRoundRect(x, y + 7, std::max(2, (int)(w * v)), 3, 1, on ? col::GOLD : col::MUTE);
    cv.fillCircle(x + (int)(w * v), y + 8, 3, on ? col::GOLD : col::MUTE);
  }

  void toggle(int y, bool v, bool on) {
    int x = SW - 34;
    cv.fillRoundRect(x, y + 3, 22, 11, 5, v ? blend565(col::INK, col::GOLD, on ? 0.7f : 0.4f) : col::INK3);
    cv.fillCircle(v ? x + 16 : x + 5, y + 8, 4, v ? col::WHITE : col::MUTE);
  }

  void drawValue(int i, int y, bool on) {
    char b[48];
    uint16_t c = on ? col::GOLD : col::MUTE;
    switch (i) {
      case R_BIRTH:
        if (birthdaySet()) snprintf(b, sizeof b, "%04d-%02d-%02d", S.birthY, S.birthM, S.birthD);
        else strcpy(b, "未设置");
        break;
      case R_LIFE: snprintf(b, sizeof b, "‹ %d 岁 ›", S.lifespan); break;
      case R_CLOCK: {
        struct tm tm;
        if (timeLocal(tm)) snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        else strcpy(b, "未设置");
      } break;
      case R_SSID: strlcpy(b, S.ssid[0] ? S.ssid : "未设置", sizeof b); break;
      case R_PASS: strcpy(b, S.pass[0] ? "••••••" : "未设置"); break;
      case R_SYNC: {
        int n = ntpState();
        const char* st[] = {timeState() == TIME_NTP ? "已同步" : "", "同步中…", "已同步", "失败"};
        strcpy(b, st[n]);
        if (n == NTP_BUSY) c = col::GOLD;
      } break;
      case R_TZ: snprintf(b, sizeof b, "‹ UTC%+d%s ›", S.tzMin / 60, S.tzMin % 60 ? ":30" : ""); break;
      case R_BRIGHT: bar(y, S.brightness / 255.0f, on); return;
      case R_VOL: bar(y, S.volume / 255.0f, on); return;
      case R_SFX: toggle(y, S.sfx, on); return;
      case R_RATE: snprintf(b, sizeof b, "‹ %.1f kHz ›", kRates[S.rateIdx] / 1000.0f); break;
      case R_GAIN: {
        const char* g[] = {"低", "中", "高", "极高"};
        snprintf(b, sizeof b, "‹ %s ›", g[S.gainIdx]);
      } break;
      case R_RECDIM: toggle(y, S.recDim, on); return;
      case R_RECOFF:
        if (kRecOff[S.recOffIdx]) snprintf(b, sizeof b, "‹ %d 秒 ›", kRecOff[S.recOffIdx]);
        else strcpy(b, "‹ 不熄屏 ›");
        break;
      case R_USBAUTO: toggle(y, S.usbAuto, on); return;
      case R_WIFIOFF: toggle(y, S.wifiAutoOff, on); return;
      case R_BLE: toggle(y, S.ble, on); return;
      case R_NTPMODE: {
        const char* m[] = {"关闭", "断电后", "每天"};
        snprintf(b, sizeof b, "‹ %s ›", m[S.ntpMode]);
      } break;
      case R_POMOOFF:
        if (kPomoOff[S.pomoOffIdx]) snprintf(b, sizeof b, "‹ %d 秒 ›", kPomoOff[S.pomoOffIdx]);
        else strcpy(b, "‹ 不熄屏 ›");
        break;
      case R_USB: strcpy(b, "Enter"); break;
      case R_POMOAUTO: toggle(y, S.pomoAuto, on); return;
      case R_STANDBY: strcpy(b, "长按 G0"); break;
      case R_ABOUT: strcpy(b, "天机 1.0"); break;
      default: b[0] = 0;
    }
    text(b, SW - 12, y + 1, &F12, c, AR);
  }

  void adjust(int d) {
    switch (sel) {
      case R_LIFE: S.lifespan = std::max(1, std::min(150, S.lifespan + d)); break;
      case R_TZ:
        S.tzMin = std::max(-720, std::min(840, S.tzMin + d * 60));
        timeApplyTz();
        break;
      case R_BRIGHT: S.brightness = std::max(10, std::min(255, S.brightness + d * 20)); break;
      case R_VOL:
        S.volume = std::max(0, std::min(255, S.volume + d * 20));
        M5.Speaker.setVolume(S.volume);
        break;
      case R_SFX: S.sfx = !S.sfx; break;
      case R_RATE: S.rateIdx = (S.rateIdx + 4 + d) % 4; break;
      case R_GAIN: S.gainIdx = (S.gainIdx + 4 + d) % 4; break;
      case R_RECDIM: S.recDim = !S.recDim; break;
      case R_RECOFF: S.recOffIdx = (S.recOffIdx + 4 + d) % 4; break;
      case R_USBAUTO: S.usbAuto = !S.usbAuto; break;
      case R_WIFIOFF: S.wifiAutoOff = !S.wifiAutoOff; break;
      case R_BLE:
        S.ble = !S.ble;
        settingsSave();
        toast(S.ble ? "翻页器重启后可用" : "已关闭，重启后释放内存", col::GOLD);
        restartAt = millis() + 1400;  // the Bluetooth RAM reservation is decided at boot
        break;
      case R_NTPMODE: S.ntpMode = (S.ntpMode + 3 + d) % 3; break;
      case R_POMOOFF: S.pomoOffIdx = (S.pomoOffIdx + 4 + d) % 4; break;
      case R_POMOAUTO: S.pomoAuto = !S.pomoAuto; break;
      default: return;
    }
    dirty = true;
    sfxTick(1.0f + 0.1f * d, 0.4f);
  }

  void activate() {
    switch (sel) {
      case R_BIRTH:
        if (birthdaySet()) snprintf(buf, sizeof buf, "%04d-%02d-%02d", S.birthY, S.birthM, S.birthD);
        else buf[0] = 0;
        break;
      case R_CLOCK: picker.begin(); return;
      case R_SSID: strlcpy(buf, S.ssid, sizeof buf); break;
      case R_PASS: strlcpy(buf, S.pass, sizeof buf); break;
      case R_SYNC:
        if (dirty) settingsSave(), dirty = false;
        ntpStart();
        if (ntpState() == NTP_FAIL) toast("先设置 WiFi", col::CINNABAR);
        return;
      case R_USB: openUsbDrive(false); return;
      case R_STANDBY:
        if (dirty) settingsSave(), dirty = false;
        if (pomo::phase() != pomo::IDLE) toast("番茄钟计时中", col::CINNABAR);
        else standbyEnter();
        return;
      default: adjust(1); return;
    }
    editing = sel;
    fresh = true;  // the first keystroke replaces the old value
    err[0] = 0;
    inputTextMode(true);
  }

  bool keyEdit(const KeyEvent& e) {
    size_t l = strlen(buf);
    if (e.key == K_ESC) {
      editing = -1;
      inputTextMode(false);
    } else if (e.key == K_BACK) {
      fresh = false;
      if (l) buf[l - 1] = 0;
    } else if (e.key == K_ENTER) {
      commit();
    } else if (e.ch >= 32 && e.ch < 127 && l < 64) {
      if (fresh) buf[0] = 0, l = 0;
      fresh = false;
      buf[l] = e.ch;
      buf[l + 1] = 0;
    }
    return true;
  }

  void commit() {
    int y, mo, d;
    switch (editing) {
      case R_BIRTH:
        if ((sscanf(buf, "%d-%d-%d", &y, &mo, &d) == 3 || (strlen(buf) == 8 && sscanf(buf, "%4d%2d%2d", &y, &mo, &d) == 3)) &&
            y > 1900 && y < 2100 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31) {
          S.birthY = y;
          S.birthM = mo;
          S.birthD = d;
        } else {
          strcpy(err, "格式 1990-01-31");
          return;
        }
        break;
      case R_SSID: strlcpy(S.ssid, buf, sizeof S.ssid); break;
      case R_PASS: strlcpy(S.pass, buf, sizeof S.pass); break;
    }
    dirty = true;
    settingsSave();
    dirty = false;
    editing = -1;
    inputTextMode(false);
    sfxChime(1.3f, 0.4f);
  }

  void drawEditor() {
    darkenRect(0, 0, SW, SH, 0.35f);
    int x = 14, y = 34, w = SW - 28, h = 66;
    roundPanel(x, y, w, h, 8, col::INK2, col::GOLD2);
    text(kNames[editing], x + 10, y + 6, &F12, col::GOLD);
    cv.drawRoundRect(x + 8, y + 24, w - 16, 20, 4, col::LINE);
    // show the tail of long input
    const char* shown = buf;
    while (textW(shown, &F12) > w - 30 && *shown) ++shown;
    if (fresh && buf[0]) cv.fillRect(x + 12, y + 27, textW(shown, &F12) + 2, 14, blend565(col::INK2, col::GOLD2, 0.5f));
    text(shown, x + 13, y + 26, &F12, col::BONE);
    int cx = x + 13 + textW(shown, &F12) + 1;
    if ((millis() / 450) & 1) cv.fillRect(cx, y + 28, 1, 12, col::GOLD);
    text(err[0] ? err : "Enter 确认 · Fn+` 取消", x + w / 2, y + 48, &F12, err[0] ? col::CINNABAR : col::MUTE, AC);
  }
};

}  // namespace

App* makeSettings() { return new Settings_(); }
