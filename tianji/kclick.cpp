// Kindle page turner (BLE HID "Kindle Click" remote), NimBLE host.
//
// The advertising payload, GATT layout, HID report map and the "wait until the Kindle has finished
// connecting before sending keys" logic are adapted from zuyan9/Open-Kindle-Click-ESP32-C6:
//   Copyright (c) 2026 zuyan9, MIT License (licenses/MIT-Open-Kindle-Click-ESP32-C6.txt).
// What the Kindle checks is documented in zuyan9/Open-Kindle-Click-nRF52840 PROTOCOL.md.
#include "kclick.h"
#include <esp_bt.h>
#include <esp_mac.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_store.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include "system.h"

#include <Preferences.h>

extern "C" void ble_store_config_init(void);

// The Arduino core frees the Bluetooth controller's memory (~36 KB) at boot unless a BT
// library claims it. We drive NimBLE directly, so claim it here, but only when the
// page turner is enabled in settings; otherwise that memory goes to everything else.
static int bootBt = -1;  // what the core was told at boot (it asks once, before setup)
extern "C" bool btInUse(void) {
  if (bootBt < 0) {
    Preferences p;
    bootBt = p.begin("tianji", true) ? p.getBool("ble", true) : true;
    p.end();
  }
  return bootBt;
}
static bool btInUseAtBoot() { return bootBt != 0; }

namespace kclick {

static const char* const NAME = "TianJi Click";

// ADV_IND: Flags (LE general, BR/EDR unsupported), Amazon company 0x0171 + PID 0x0443 (big-endian)
// as the first manufacturer AD, HID service UUID. All of it must be in the advert itself.
static const uint8_t ADV_DATA[] = {0x02, 0x01, 0x06, 0x05, 0xFF, 0x71, 0x01, 0x04, 0x43, 0x03, 0x03, 0x12, 0x18};
static const uint8_t PNP_ID[7] = {0x01, 0x71, 0x01, 0x43, 0x04, 0x00, 0x01};  // SIG source, VID, PID, version
static const uint8_t HID_INFO[4] = {0x11, 0x01, 0x00, 0x03};
static const uint8_t REPORT_REF_IN1[2] = {0x01, 0x01};
static const char DIS_MFG[] = "TianJi";
static const char DIS_MODEL[] = "Cardputer ADV";
static const char DIS_FW[] = "TianJi 1.0";

// One input report (ID 1): boot-keyboard layout + one 16-bit consumer usage, all in one
// application collection so the Kindle's evdev node gets autorepeat for brightness.
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x75, 0x08, 0x95, 0x01, 0x81, 0x01, 0x19, 0x00,
    0x29, 0x65, 0x15, 0x00, 0x25, 0x65, 0x75, 0x08, 0x95, 0x06, 0x81, 0x00, 0x05, 0x0C, 0x19, 0x00,
    0x2A, 0xFF, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0};

struct KeyDef {
  bool consumer;
  uint16_t usage;
};
static const KeyDef KEYS[KEY_COUNT] = {
    {false, 0x4E},   // KEY_PAGEDOWN: page forward
    {false, 0x4B},   // KEY_PAGEUP: page back
    {true, 0x006F},  // brightness up
    {true, 0x0070},  // brightness down
    {true, 0x0030},  // power: put the Kindle to sleep
};

static constexpr uint16_t ADV_FAST = 32;   // 20 ms: reconnects in a couple of seconds
static constexpr uint16_t ADV_SLOW = 244;  // 152.5 ms
static constexpr uint32_t FAST_FOR_MS = 60000;
static constexpr uint32_t DORMANT_MS = 10UL * 60 * 1000;  // stop advertising when nobody comes
static constexpr uint32_t PENDING_TTL_MS = 10000;
static constexpr uint32_t READY_QUIET_MS = 800, READY_FALLBACK_MS = 5000;

static bool running = false;
static volatile bool synced = false;
static uint8_t ownAddrType = BLE_OWN_ADDR_PUBLIC;
static uint16_t reportHandle = 0, batteryHandle = 0;
static uint8_t lastReport[10] = {0};
static uint8_t ctrlPoint = 0;
static uint8_t battery = 100;

static volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool secured = false, hidNotify = false, batNotify = false, fastItvl = false;
static volatile uint32_t connectedAt = 0, securedAt = 0, cccdAt = 0, discDoneAt = 0;
static bool ready = false, paramsAsked = false, batterySent = false;
static uint32_t readyAt = 0;
static volatile bool autoAdv = true, dormant = false, bondsCached = false;
static volatile uint16_t advItvl = ADV_FAST;
static uint32_t fastSince = 0, lostSince = 0;

static bool down[KEY_COUNT];
static uint32_t downAt[KEY_COUNT];
static bool dirty = false;
static int pending = -1;
static uint32_t pendingAt = 0;
static uint32_t pageCount = 0;

static bool connected() { return conn != BLE_HS_CONN_HANDLE_NONE; }

static bool hasBonds() {
  if (!synced) return false;
  int c = 0;
  if (ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &c) == 0 && c > 0) return true;
  if (ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &c) == 0 && c > 0) return true;
  return false;
}

// ---------------------------------------------------------------- GATT

static const ble_uuid16_t U_HID = BLE_UUID16_INIT(0x1812), U_REPORT = BLE_UUID16_INIT(0x2A4D),
                          U_RPTREF = BLE_UUID16_INIT(0x2908), U_MAP = BLE_UUID16_INIT(0x2A4B),
                          U_INFO = BLE_UUID16_INIT(0x2A4A), U_CTRL = BLE_UUID16_INIT(0x2A4C),
                          U_BAS = BLE_UUID16_INIT(0x180F), U_BATLVL = BLE_UUID16_INIT(0x2A19),
                          U_DIS = BLE_UUID16_INIT(0x180A), U_MODEL = BLE_UUID16_INIT(0x2A24),
                          U_FW = BLE_UUID16_INIT(0x2A26), U_MFG = BLE_UUID16_INIT(0x2A29),
                          U_PNP = BLE_UUID16_INIT(0x2A50);

static int onAccess(uint16_t, uint16_t, struct ble_gatt_access_ctxt* ctxt, void*) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
    if (ble_uuid_u16(ctxt->dsc->uuid) == 0x2908)
      return os_mbuf_append(ctxt->om, REPORT_REF_IN1, sizeof REPORT_REF_IN1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    return BLE_ATT_ERR_UNLIKELY;
  }
  uint16_t u = ble_uuid_u16(ctxt->chr->uuid);
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    if (u == 0x2A4C && OS_MBUF_PKTLEN(ctxt->om) >= 1) {
      os_mbuf_copydata(ctxt->om, 0, 1, &ctrlPoint);
      return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
  const void* d = nullptr;
  size_t n = 0;
  switch (u) {
    case 0x2A4D: d = lastReport, n = sizeof lastReport; break;
    case 0x2A4B: d = REPORT_MAP, n = sizeof REPORT_MAP; break;
    case 0x2A4A: d = HID_INFO, n = sizeof HID_INFO; break;
    case 0x2A19: d = &battery, n = 1; break;
    case 0x2A24: d = DIS_MODEL, n = strlen(DIS_MODEL); break;
    case 0x2A26: d = DIS_FW, n = strlen(DIS_FW); break;
    case 0x2A29: d = DIS_MFG, n = strlen(DIS_MFG); break;
    case 0x2A50: d = PNP_ID, n = sizeof PNP_ID; break;
    default: return BLE_ATT_ERR_UNLIKELY;
  }
  return os_mbuf_append(ctxt->om, d, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static struct ble_gatt_dsc_def REPORT_DSCS[] = {
    {.uuid = &U_RPTREF.u, .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC, .min_key_size = 0, .access_cb = onAccess, .arg = nullptr},
    {0}};

#define CHR(uu, fl, vh, ds) \
  { .uuid = &(uu).u, .access_cb = onAccess, .arg = nullptr, .descriptors = ds, .flags = fl, .min_key_size = 0, .val_handle = vh, .cpfd = nullptr }

static const struct ble_gatt_chr_def HID_CHRS[] = {
    CHR(U_REPORT, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY, &reportHandle, REPORT_DSCS),
    CHR(U_MAP, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC, nullptr, nullptr),
    CHR(U_INFO, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC, nullptr, nullptr),
    CHR(U_CTRL, BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC, nullptr, nullptr),
    {0}};
static const struct ble_gatt_chr_def BAS_CHRS[] = {CHR(U_BATLVL, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, &batteryHandle, nullptr), {0}};
static const struct ble_gatt_chr_def DIS_CHRS[] = {
    CHR(U_MFG, BLE_GATT_CHR_F_READ, nullptr, nullptr), CHR(U_MODEL, BLE_GATT_CHR_F_READ, nullptr, nullptr),
    CHR(U_FW, BLE_GATT_CHR_F_READ, nullptr, nullptr), CHR(U_PNP, BLE_GATT_CHR_F_READ, nullptr, nullptr), {0}};
// HID first so its handles never move.
static const struct ble_gatt_svc_def SVCS[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &U_HID.u, .includes = nullptr, .characteristics = HID_CHRS},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &U_BAS.u, .includes = nullptr, .characteristics = BAS_CHRS},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &U_DIS.u, .includes = nullptr, .characteristics = DIS_CHRS},
    {0}};

// ---------------------------------------------------------------- reports

// The Kindle needs a moment after encryption (GATT rediscovery, CCCD writes) before keys count.
static bool linkReady() {
  if (!connected() || !secured) return false;
  if (ready) return true;
  uint32_t now = millis(), c = cccdAt, d = discDoneAt;
  uint32_t last = !c ? d : !d ? c : (int32_t)(c - d) > 0 ? c : d;
  bool quiet = last && !fastItvl && (int32_t)(now - last) >= (int32_t)READY_QUIET_MS;
  if (quiet || now - securedAt >= READY_FALLBACK_MS) {
    ready = true;
    readyAt = now;
  }
  return ready;
}

static bool sendReport() {
  uint16_t c = conn;
  if (c == BLE_HS_CONN_HANDLE_NONE || !hidNotify) return false;
  uint8_t r[10] = {0};
  int n = 2;
  uint16_t usage = 0;
  for (int k = 0; k < KEY_COUNT; ++k) {
    if (!down[k]) continue;
    if (KEYS[k].consumer) usage = KEYS[k].usage;
    else if (n < 8) r[n++] = (uint8_t)KEYS[k].usage;
  }
  r[8] = usage & 0xFF;
  r[9] = usage >> 8;
  memcpy(lastReport, r, sizeof r);
  struct os_mbuf* om = ble_hs_mbuf_from_flat(r, sizeof r);
  return om && ble_gatts_notify_custom(c, reportHandle, om) == 0;
}

static void flush() {
  if (!connected()) {
    dirty = false;
    return;
  }
  if (dirty && sendReport()) dirty = false;
}

static void notifyBattery() {
  uint16_t c = conn;
  if (c == BLE_HS_CONN_HANDLE_NONE || !batNotify) return;
  struct os_mbuf* om = ble_hs_mbuf_from_flat(&battery, 1);
  if (om && ble_gatts_notify_custom(c, batteryHandle, om) == 0) batterySent = true;
}

// ---------------------------------------------------------------- GAP

static int onGap(struct ble_gap_event* ev, void*);

static void advertise(uint16_t itvl) {
  if (!synced || connected() || !running) return;
  dormant = false;
  if (ble_gap_adv_active()) {
    if (advItvl == itvl) return;
    ble_gap_adv_stop();
  }
  if (ble_gap_adv_set_data(ADV_DATA, sizeof ADV_DATA) != 0) return;
  struct ble_hs_adv_fields rsp = {};
  rsp.name = (const uint8_t*)NAME;
  rsp.name_len = strlen(NAME);
  rsp.name_is_complete = 1;
  if (ble_gap_adv_rsp_set_fields(&rsp) != 0) return;
  struct ble_gap_adv_params p = {};
  p.conn_mode = BLE_GAP_CONN_MODE_UND;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  p.itvl_min = p.itvl_max = itvl;
  advItvl = itvl;
  if (itvl == ADV_FAST) fastSince = millis();
  ble_gap_adv_start(ownAddrType, nullptr, BLE_HS_FOREVER, &p, onGap, nullptr);
}

static void onDisconnected() {
  conn = BLE_HS_CONN_HANDLE_NONE;
  secured = ready = hidNotify = batNotify = false;
  memset(down, 0, sizeof down);
  dirty = false;
  lostSince = millis();
  if (autoAdv && !dormant) advertise(ADV_FAST);
}

static int onGap(struct ble_gap_event* ev, void*) {
  switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (ev->connect.status != 0) {
        if (autoAdv) advertise(ADV_FAST);
        return 0;
      }
      connectedAt = millis();
      cccdAt = discDoneAt = 0;
      secured = ready = paramsAsked = batterySent = hidNotify = batNotify = false;
      conn = ev->connect.conn_handle;
      if (ble_gap_adv_active()) ble_gap_adv_stop();
      {
        struct ble_gap_conn_desc d;
        fastItvl = ble_gap_conn_find(conn, &d) == 0 && d.conn_itvl == 6;
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT: onDisconnected(); return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
      if (ev->conn_update.status == 0) {
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0) {
          bool fast = d.conn_itvl == 6;
          if ((fastItvl && !fast) || (d.conn_itvl == 16 && d.conn_latency == 0)) discDoneAt = millis() | 1;
          fastItvl = fast;
        }
      }
      return 0;
    case BLE_GAP_EVENT_ENC_CHANGE: {
      struct ble_gap_conn_desc d;
      if (ev->enc_change.status == 0 && ble_gap_conn_find(ev->enc_change.conn_handle, &d) == 0 && d.sec_state.encrypted) {
        securedAt = millis();
        secured = true;
        bondsCached = true;
      }
      return 0;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
      if (ev->subscribe.attr_handle == reportHandle) hidNotify = ev->subscribe.cur_notify;
      else if (ev->subscribe.attr_handle == batteryHandle) batNotify = ev->subscribe.cur_notify;
      if (ev->subscribe.reason == BLE_GAP_SUBSCRIBE_REASON_WRITE && ev->subscribe.cur_notify) cccdAt = millis() | 1;
      return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      struct ble_gap_conn_desc d;
      if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_ADV_COMPLETE:
      if (autoAdv && !dormant && !connected() && ev->adv_complete.reason != 0) advertise(advItvl);
      return 0;
    default: return 0;
  }
}

static void onSync() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  // static random address derived from the BT MAC (two top bits set), little-endian
  uint8_t le[6] = {mac[5], mac[4], mac[3], mac[2], mac[1], (uint8_t)(mac[0] | 0xC0)};
  if (ble_hs_id_set_rnd(le) == 0) ownAddrType = BLE_OWN_ADDR_RANDOM;
  else {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &ownAddrType);
  }
  synced = true;
  bondsCached = hasBonds();
  advertise(ADV_FAST);
}

static void onReset(int) { synced = false; }

static void hostTask(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

// ---------------------------------------------------------------- public

bool begin() {
  if (running) return true;
  if (!btInUseAtBoot()) return false;  // memory was handed to the heap at boot: needs a restart
  if (nimble_port_init() != ESP_OK) return false;
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P6);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P6);
  ble_hs_cfg.reset_cb = onReset;
  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  // Just Works, bonding, legacy pairing: the Kindle needs encryption but no MITM
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 0;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set(NAME);
  if (ble_gatts_count_cfg(SVCS) != 0 || ble_gatts_add_svcs(SVCS) != 0) {
    nimble_port_deinit();
    return false;
  }
  ble_store_config_init();
  running = true;
  autoAdv = true;
  dormant = false;
  lostSince = millis();
  nimble_port_freertos_init(hostTask);
  return true;
}

void end() {
  if (!running) return;
  autoAdv = false;
  if (connected()) {
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    uint32_t t0 = millis();
    while (connected() && millis() - t0 < 1500) delay(10);
  }
  if (ble_gap_adv_active()) ble_gap_adv_stop();
  nimble_port_stop();
  nimble_port_deinit();
  running = synced = false;
  conn = BLE_HS_CONN_HANDLE_NONE;
}

bool on() { return running; }
bool bonded() { return bondsCached; }

State state() {
  if (!running) return OFF;
  if (connected()) return linkReady() ? READY : CONNECTED;
  return bondsCached ? WAITING : PAIRING;
}

void forget() {
  if (!running) return;
  pending = -1;
  autoAdv = false;
  if (ble_gap_adv_active()) ble_gap_adv_stop();
  if (connected()) {
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    uint32_t t0 = millis();
    while (connected() && millis() - t0 < 2000) delay(10);
  }
  ble_store_clear();
  bondsCached = false;
  autoAdv = true;
  advertise(ADV_FAST);
}

void wake() {
  autoAdv = true;
  lostSince = millis();
  if (running && !connected()) advertise(ADV_FAST);
}

bool tap(Key k) {
  if (!running) return false;
  if (!linkReady()) {
    wake();
    if (k == NEXT || k == PREV) {
      pending = k;  // deliver once the Kindle reconnects
      pendingAt = millis();
    }
    return false;
  }
  down[k] = true;
  downAt[k] = millis();
  dirty = true;
  flush();
  if (k == NEXT || k == PREV) ++pageCount;
  return true;
}

uint32_t pages() { return pageCount; }

void service() {
  if (!running) return;
  uint32_t now = millis();
  // releases after a short press
  for (int k = 0; k < KEY_COUNT; ++k)
    if (down[k] && now - downAt[k] >= 40) {
      down[k] = false;
      dirty = true;
    }
  flush();
  if (pending >= 0) {
    if (now - pendingAt > PENDING_TTL_MS) pending = -1;
    else if (linkReady()) {
      int k = pending;
      pending = -1;
      tap((Key)k);
    }
  }
  if (connected()) {
    if (linkReady() && !paramsAsked && now - readyAt > 1500) {
      paramsAsked = true;
      struct ble_gap_upd_params p = {};
      p.itvl_min = p.itvl_max = 16;  // 20 ms like the Kindle, latency 4, 3 s supervision
      p.latency = 4;
      p.supervision_timeout = 300;
      ble_gap_update_params(conn, &p);
    }
    static uint32_t batAt = 0;
    if (linkReady() && (!batterySent || now - batAt > 15UL * 60 * 1000)) {
      battery = batteryPct();
      notifyBattery();
      batAt = now;
    }
    return;
  }
  // power: fast adverts for a minute, then slow, then sleep until woken
  if (synced && autoAdv && !dormant) {
    if (advItvl == ADV_FAST && now - fastSince > FAST_FOR_MS) advertise(ADV_SLOW);
    if (now - lostSince > DORMANT_MS) {
      dormant = true;
      if (ble_gap_adv_active()) ble_gap_adv_stop();
    } else if (!ble_gap_adv_active()) {
      advertise(advItvl);
    }
  }
}

}  // namespace kclick
