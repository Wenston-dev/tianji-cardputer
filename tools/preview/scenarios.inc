  // included into main() for the full build
  if (want("qian")) {
    go(APP_QIAN); sim(0.6f); snap("qian_idle");
    g_held.push_back(32); sim(1.3f); snap("qian_shake");
    g_held.clear(); sim(0.4f); snap("qian_eject");
    sim(1.6f); snap("qian_fall");
    sim(0.5f); snap("qian_label");
    sim(0.9f); sim(5.0f); snap("qian_slip");
    press(K_ENTER); sim(0.8f); snap("qian_detail");
    go(APP_HOME);
  }
  if (want("jiao")) {
    go(APP_JIAO); sim(0.5f); snap("jiao_idle");
    g_held.push_back(32); sim(0.7f); snap("jiao_charge"); g_held.clear(); sim(0.35f); snap("jiao_fly");
    sim(4.0f); snap("jiao_done");
    go(APP_HOME);
  }
  if (want("dice")) {
    go(APP_DICE); press('3'); sim(0.5f); snap("dice_idle");
    g_held.push_back(32); sim(0.8f); snap("dice_charge"); g_held.clear(); sim(0.3f); snap("dice_roll");
    sim(4.0f); snap("dice_done");
    press('6'); sim(0.1f); press(' '); sim(5.0f); snap("dice_six");
    go(APP_HOME);
  }
  if (want("coin")) {
    go(APP_COIN); sim(0.5f); snap("coin_idle");
    g_held.push_back(32); sim(1.2f); snap("coin_charge"); g_held.clear(); sim(0.25f); snap("coin_fly");
    sim(0.6f); snap("coin_land");
    sim(4.0f); snap("coin_done");
    go(APP_HOME);
  }
  if (want("rec")) {
    go(APP_REC); sim(0.5f); snap("rec_idle");
    press(K_ENTER); sim(4.0f); snap("rec_live");
    press(' '); sim(0.6f); snap("rec_pause");
    rec::g_state = rec::IDLE;
    go(APP_HOME);
  }
  if (want("lib")) {
    go(APP_LIB); sim(0.5f); press(K_DOWN); sim(0.8f); snap("lib_list");
    press('d'); sim(0.3f); snap("lib_del");
    press(K_UP); press(K_ENTER); sim(2.0f); snap("lib_play");
    go(APP_HOME);
  }
  if (want("set")) {
    go(APP_SET); sim(0.4f); snap("set_list");
    press(K_ENTER); type("1992-05-20"); sim(0.3f); snap("set_edit");
    press(K_ENTER); for (int i = 0; i < 13; ++i) press(K_DOWN); sim(0.6f); snap("set_low");
    go(APP_HOME);
  }
  if (want("usb")) {
    openUsbDrive(true); sim(1.5f); snap("usb_ask");
    sim(2.5f); sim(1.0f); snap("usb_active");
    go(APP_HOME);
  }
  if (want("pomo")) {
    go(APP_POMO); sim(0.5f); snap("pomo_list");
    press(K_TAB); sim(1.0f); snap("pomo_stats"); press(K_ESC);
    press('e'); sim(0.3f); snap("pomo_edit"); press(K_ESC);
    press(K_ENTER); sim(1.0f); snap("pomo_run");
    go(APP_HOME); sim(0.5f); snap("pomo_home");
    go(APP_DICE); sim(0.3f); snap("pomo_badge");
    pomo::stop(); go(APP_HOME);
  }
  if (want("kindle")) {
    go(APP_KINDLE); sim(0.5f); snap("kindle_ready");
    press(K_RIGHT); sim(0.12f); snap("kindle_flip");
    kclick::g_state = 1; sim(0.6f); snap("kindle_pair");
    kclick::g_state = 0; sim(0.3f); snap("kindle_off");
    kclick::g_state = 4; go(APP_HOME); press(K_ENTER); sim(0.6f); snap("kindle_dock");
  }
  if (want("time")) {
    extern int g_tstate;
    g_tstate = TIME_RESTORED;
    go(APP_HOME); sim(1.2f); snap("time_hint");
    press('t'); sim(0.5f); snap("time_picker");
    press(K_UP); press(K_UP); press(K_RIGHT); press('4'); press('5'); sim(0.3f); snap("time_picker_edit");
    press(K_ENTER); sim(0.4f); snap("time_set");
    g_tstate = TIME_NTP;
    go(APP_SET); for (int i = 0; i < 21; ++i) press(K_DOWN); sim(0.8f); snap("set_standby");
    go(APP_HOME);
  }
