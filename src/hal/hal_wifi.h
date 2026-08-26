// hal_wifi.h — Wi-Fi + BLE radio abstraction
//
// ───────────────────────────────────────────────────────────────────────────
//  ARCHITECTURE
// ───────────────────────────────────────────────────────────────────────────
//
//  On Raspberry Pi 5, the Wi-Fi/BLE radio is the onboard BCM43455 (or
//  BCM43456 on later revisions), exposed through the standard nl80211
//  netlink interface. There is no Arduino-friendly library — the radio is
//  managed out-of-band and the kernel either passes 802.11 frames to
//  userspace via a monitor-mode VIF or accepts netlink commands for
//  scanning / connecting.
//
//  This HAL is therefore a thin wrapper around three kernel facilities:
//
//      1. nl80211 netlink socket (mode set, scan trigger, station join)
//      2. AF_PACKET socket on a monitor-mode VIF (raw 802.11 frames)
//      3. Host Controller Interface (HCI) via /dev/bluetooth for BLE
//
//  The HAL never assumes the radio is present. Every call checks the
//  capability flag and returns false on a Pi variant that lacks Wi-Fi.
//
//  On ESP32 the same surface is implemented with the IDF Wi-Fi and BLE
//  APIs (esp_wifi_*, esp_bt_*, esp_ble_gap_*). The semantics are preserved
//  across both targets; only the transport differs.
//
// ───────────────────────────────────────────────────────────────────────────
//  CAPABILITY BITMASK
// ───────────────────────────────────────────────────────────────────────────
//
//   bit 0  WIFI_CAP_STA       — station (client) mode
//   bit 1  WIFI_CAP_MONITOR   — 802.11 monitor mode
//   bit 2  WIFI_CAP_AP        — soft AP (Rogue AP feature)
//   bit 3  WIFI_CAP_BLE_SCAN  — passive BLE advertising scan
//   bit 4  WIFI_CAP_BLE_ADV   — BLE advertising (BadBLE HID)
//   bit 5  WIFI_CAP_BLE_GATT  — GATT client (read/write/notify)
//
//  hal_wifi_capabilities() returns this mask. UI code uses it to grey out
//  unsupported menu items before they are picked.
//
// ───────────────────────────────────────────────────────────────────────────
//  STEALTH MODE (one-shot reconfiguration)
// ───────────────────────────────────────────────────────────────────────────
//
//  hal_wifi_set_stealth(true) performs the following on every supported
//  radio simultaneously:
//
//      * Set a random locally administered MAC address on every boot.
//        The LAA bit (bit 1 of byte 0) is set; the multicast bit (bit 0)
//        is cleared. The new address is written via NL80211_SET_MAC.
//      * Set the interface to monitor mode (NL80211_IFTYPE_MONITOR) with
//        no Tx bit set so probe requests are never emitted.
//      * Set the DHCP hostname (option 12) to a 12-byte random hex string
//        and rotate it on every reconnect.
//      * Drop multicast memberships on mDNS (224.0.0.251), LLMNR
//        (224.0.0.252) and NetBIOS (UDP/137) by joining the all-zero
//        multicast group only, so the kernel drops those frames.
//
//  hal_wifi_set_stealth(false) restores the previous interface state. The
//  stealth state survives across app launches but never across reboots —
//  the random MAC is regenerated each time the kernel brings the
//  interface back up.
//
// ───────────────────────────────────────────────────────────────────────────
//  WHAT MUST BE ADDED BEFORE THIS MODULE IS USABLE
// ───────────────────────────────────────────────────────────────────────────
//
//  1. nl80211 socket bring-up:
//        struct nl_sock *sk = nl_socket_alloc();
//        nl_connect(sk, NETLINK_GENERIC);
//        genl_ctrl_resolve(sk, "nl80211");
//     The libnl dependency should be marked optional; if it is not
//     installed at build time the module compiles but every call returns
//     false.
//
//  2. Monitor-mode VIF: `iw phy phy0 interface add mon0 type monitor`.
//     Store the VIF name so frames can be bound to AF_PACKET.
//
//  3. AF_PACKET socket: socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)),
//     bound to mon0 with TPACKET_V3 for block-aligned receive.
//
//  4. BLE scan: open the HCI socket via ioctl(HCIUP, HCIUARTSETFLAGS) or
//     by using the BlueZ D-Bus API. HCI is preferred for raw advertising
//     reports.
//
//  5. ESP32 path: the IDF equivalents are listed at the top of each
//     function. Where the Pi 5 path uses nl80211 cmd IDs, the ESP32 path
//     uses ESP-IDF enum values from <esp_wifi_types.h>.
//
// ───────────────────────────────────────────────────────────────────────────
//  RESOURCE NOTES (Pi 5, 4 GB)
// ───────────────────────────────────────────────────────────────────────────
//
//   * nl80211 socket:    ~32 KB kernel side, ~4 KB userspace
//   * AF_PACKET TPACKET_V3 ring: 256 KB at 4 Mbps throughput; 1 MB at
//     150 Mbps (channel utilisation drops the frame rate).
//   * HCI socket:        ~16 KB
//   * Heap per scan result: ~96 bytes; budget cap is WIFI_AP_LOG.
//
// ───────────────────────────────────────────────────────────────────────────

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Mirrors of constants in config.h so this header can stand alone. Keep
// these values in sync with config.h if either is changed.
#define WIFI_AP_LOG         32
#define BLE_DEVICE_LOG      16
#define GATT_HANDLE_LOG     32
#define PCAP_SNAP_LEN       256

// Capability bitmask returned by hal_wifi_capabilities().
#define WIFI_CAP_STA         0x01
#define WIFI_CAP_MONITOR     0x02
#define WIFI_CAP_AP          0x04
#define WIFI_CAP_BLE_SCAN    0x08
#define WIFI_CAP_BLE_ADV     0x10
#define WIFI_CAP_BLE_GATT    0x20

// Wi-Fi security standards encoded in WifiApRecord.security.
#define WIFI_SEC_OPEN        0x00
#define WIFI_SEC_WEP         0x01
#define WIFI_SEC_WPA         0x02
#define WIFI_SEC_WPA2        0x04
#define WIFI_SEC_WPA3        0x08
#define WIFI_SEC_OWE         0x10
#define WIFI_SEC_UNKNOWN     0x80

// BLE address types — match the GAP specification.
typedef enum {
    BLE_ADDR_PUBLIC = 0,
    BLE_ADDR_RANDOM = 1,
    BLE_ADDR_RPA    = 2,   // Resolvable Private Address
    BLE_ADDR_NRPA   = 3,   // Non-Resolvable Private Address
} BleAddrType;

// One captured access point. Fields are populated by hal_wifi_scan_step().
typedef struct {
    uint8_t  bssid[6];
    char     ssid[33];        // 32 + NUL
    int8_t   rssi;            // dBm
    uint8_t  channel;         // primary channel (1..165)
    uint8_t  security;        // bitmask of WIFI_SEC_*
    uint16_t beacon_int;      // TU
    uint8_t  wps;             // 1 if WPS enabled (lock-down target)
    uint8_t  pmf;             // 1 if 802.11w Protected Management Frames
    uint32_t last_seen_ms;    // monotonic millis()
    float    lat, lon;        // GPS position at scan, from -1 if none
} WifiApRecord;

// One captured BLE advertisement.
typedef struct {
    uint8_t  addr[6];
    BleAddrType addr_type;
    char     name[32];        // local name (GAP 0x09) or empty
    int8_t   rssi;
    uint8_t  adv_type;        // ADV_IND, SCAN_RSP, etc.
    uint16_t appearance;      // GAP 0x19
    uint8_t  svc_uuid_count;
    uint8_t  svc_uuids[8][16];// truncated to 8 for RAM budget
    uint32_t first_seen_ms;
} BleAdvRecord;

// Lifecycle ----------------------------------------------------------------
void hal_wifi_init();
void hal_wifi_tick();                    // dispatch packets to consumers
uint8_t hal_wifi_capabilities();         // WIFI_CAP_* bitmask

// Mode control --------------------------------------------------------------
bool hal_wifi_set_mode_monitor();       // raises monitor-mode VIF
bool hal_wifi_set_mode_station();        // managed client
bool hal_wifi_set_mode_ap(const char *ssid, uint8_t channel, bool hidden);
bool hal_wifi_set_stealth(bool enable);  // see header doc
bool hal_wifi_is_stealth();

// MAC & identity -----------------------------------------------------------
void hal_wifi_get_mac(uint8_t out[6]);   // current MAC
void hal_wifi_randomize_mac();           // LAA-set random address

// Wardriving scan ----------------------------------------------------------
bool hal_wifi_scan_start();              // kicks off scan_results
bool hal_wifi_scan_step(WifiApRecord *out); // call repeatedly; false = done
uint16_t hal_wifi_scan_count();

// Frame capture (monitor mode) ---------------------------------------------
// Returns a pointer to an internal ring buffer slot. The slot is valid until
// the next hal_wifi_capture_consume() call. length includes the 802.11
// header. radiotap_header_length is the offset where the MAC payload starts.
typedef struct {
    uint32_t timestamp_us;
    int8_t   rssi;
    uint8_t  channel;
    uint16_t length;
    uint16_t radiotap_header_length;
    const uint8_t *data;
} WifiFrame;

bool hal_wifi_capture_consume(WifiFrame *out);

// 4-way handshake & PMKID logging -------------------------------------------
// The handshake logger attaches to the monitor-mode VIF and decodes EAPOL
// frames (key info 0x888E, type key). For each handshake it captures:
//   * AA (authenticator MAC) — used as the per-AP PMKID lookup key
//   * SPA (supplicant MAC)
//   * ANonce, SNonce
//   * MIC (used as a hashcat-compatible filename suffix)
//
//   The captured frames are streamed to hal_sdcard_open(...) via the
//   `handshake_file` argument to hal_wifi_handshake_capture_start().
//   Implementation note: only the first two EAPOL frames and the final
//   message are required for offline cracking; intermediate frames are
//   still logged but the writer must be careful about file growth.
typedef struct {
    uint8_t aa[6];           // authenticator
    uint8_t spa[6];          // supplicant
    uint8_t anonce[32];
    uint8_t snonce[32];
    uint8_t mic[16];
    uint8_t eapol_frames;    // 1..4 captured so far
} WifiHandshake;

bool hal_wifi_handshake_capture_start();
bool hal_wifi_handshake_step(WifiHandshake *out); // false = no new handshake
void hal_wifi_handshake_capture_stop();

// BLE scanning -------------------------------------------------------------
bool hal_ble_scan_start(bool passive_only); // see Passive-Only note below
bool hal_ble_scan_step(BleAdvRecord *out);
uint16_t hal_ble_scan_count();
void hal_ble_scan_stop();

// Passive-only note: when passive_only is true, the scanner NEVER emits
// SCAN_REQ packets. The host receives only the unsolicited advertisements
// (ADV_IND, ADV_NONCONN_IND, ADV_SCAN_IND). This is required for the
// "Passive BLE Scanning Only" stealth feature.

// BLE advertising (BadBLE HID) ---------------------------------------------
// The BadBLE HID profile emulates a Bluetooth keyboard. The keyboard
// report descriptor is taken from the USB HID 1.11 specification and is
// embedded in this header so the report format is identical across BLE
// (Bluetooth HID over GATT) and USB HID.
//
// The advertising payload advertises the standard HID service UUID
// (0x1812) and the Battery service (0x180F) so that a paired host sees
// a generic keyboard.
bool hal_ble_hid_advertise_start(const char *device_name);
bool hal_ble_hid_send_report(const uint8_t report[8]); // boot keyboard
void hal_ble_hid_advertise_stop();

// GATT client --------------------------------------------------------------
typedef struct {
    uint16_t handle;          // attribute handle
    uint16_t uuid16;          // 0 = 128-bit UUID follows in uuid128
    uint8_t  uuid128[16];
    uint8_t  properties;      // 0x01 read, 0x02 write, 0x04 notify, ...
} GattCharacteristic;

typedef struct {
    uint8_t  addr[6];
    char     name[32];
    uint8_t  service_count;
    GattCharacteristic chars[GATT_HANDLE_LOG];
} GattDevice;

bool hal_ble_gatt_enumerate(uint8_t addr[6], GattDevice *out);
bool hal_ble_gatt_read (uint8_t addr[6], uint16_t handle, uint8_t *buf, uint8_t *len);
bool hal_ble_gatt_write(uint8_t addr[6], uint16_t handle, const uint8_t *buf, uint8_t len);

// ── Raw 802.11 frame injection ────────────────────────────────────────────
//
//  hal_wifi_inject_frame sends an arbitrary 802.11 frame over a
//  monitor-mode VIF that was previously opened by hal_wifi_set_mode_monitor().
//  The frame must be a valid 802.11 MPDU; the function prepends a
//  radiotap header at the kernel level so the caller passes the
//  802.11 MAC frame (starting at the Frame Control field).
//
//  Real impl (Pi 5): libnl nl80211 + NLA_PUT_MSGC for the
//  NL80211_CMD_FRAME command:
//
//      nl_send_alloc(sk, msg, ...);
//      genlmsg_put(msg, ...);
//      NLA_PUT(msg, NL80211_ATTR_IFINDEX, ...);
//      NLA_PUT(msg, NL80211_ATTR_FRAME, frame_len, frame);
//      nla_put_u32(msg, NL80211_ATTR_DURATION, dur);
//
//  Real impl (ESP32): esp_wifi_80211_tx(wifi_interface_t ifx, void *buf,
//  int len, bool en_sys_seq) from <esp_wifi.h>.
//
//  LEGAL: 802.11 frame injection requires operator authorisation on the
//  target network. In every jurisdiction the operator MUST own the
//  spectrum they're transmitting into, or hold an amateur-radio licence
//  with the relevant band privilege. Refusing to enforce this in code
//  would make the device a "jamming tool" per FCC Part 15 / ETSI EN 301.
//
//  ETHICAL: the function takes the operator MAC as a parameter so a
//  passive audit can keep the source address constant (the audit does
//  NOT impersonate a client). Setting src_mac != operator_mac is
//  rejected by the real implementation; the skeleton only checks the
//  band and refuses if hal_wifi_is_stealth() is true.
bool hal_wifi_inject_frame(const uint8_t *frame, size_t length,
                            uint16_t duration_tu);

// ── Targeted deauth flood ────────────────────────────────────────────────
//
//  Sends `count` deauthentication frames to `target_sta` claiming to
//  come from `ap_bssid`. The frames are spaced `interval_ms` apart so
//  the operator can stop the flood with EVT_BTN_B.
//
//  Real impl: while (count--) { build_deauth_frame(); inject_frame();
//  delay(interval_ms); } Real-world deauth frames are 26 bytes:
//
//      [type=0xc0, subtype=0x03] [flags=0] [duration=0x3a]
//      [addr1=target] [addr2=ap] [addr3=ap] [seq]
//      [reason=7]      // 7 = "Class 3 frame from nonassociated STA"
//
//  The reason code 7 is the value most clients accept silently; reason
//  4 ("Disassociated due to inactivity") is the next best.
//
//  Stealth: in stealth mode the function refuses; deauth is by
//  definition detectable. The status bar must show "DEAUTH" in red.
bool hal_wifi_deauth_flood(const uint8_t ap_bssid[6],
                            const uint8_t target_sta[6],
                            uint16_t count,
                            uint16_t interval_ms);

// ── KARMA attack ──────────────────────────────────────────────────────────
//
//  KARMA responds to every probe request the radio hears with a beacon
//  for the requested SSID. A client that probes for "Starbucks" while
//  in range of a KARMA AP will receive a beacon claiming to BE Starbucks
//  and may auto-join.
//
//  Real impl: subscribe to probe-request frames on the monitor VIF,
//  extract the SSID from the request, and respond with a beacon from a
//  sibling AP-mode VIF (nl80211 allows monitor + AP simultaneously on
//  some drivers). The AP VIF advertises open authentication so the
//  client completes the L2 association. Subsequent DHCP / DNS
//  harvesting happens at the IP layer — a captive portal in user space.
//
//  Stealth: KARMA is THE most detectable Wi-Fi attack. Refused in
//  stealth mode.
bool hal_wifi_karma_start();
void hal_wifi_karma_stop();

// ── Custom BLE advertisement ──────────────────────────────────────────────
//
//  Sends an unsolicited BLE advertisement packet on advertising
//  channels 37 / 38 / 39. Used to spoof iBeacon / Eddystone / continuity
//  protocol packets so a phone sees the void-os as an Apple Watch,
//  AirPods, or whatever the operator chooses.
//
//  Real impl: HCI LE Set Advertising Data + Set Advertising Parameters
//  + Set Advertise Enable via bluez D-Bus or the kernel mgmt interface.
//
//  param bytes: 31-byte max payload as defined by the GAP spec (the
//  real impl enforces this).
bool hal_ble_advertise_custom(const uint8_t *param, size_t param_len,
                               int8_t tx_power_dbm);
void hal_ble_advertise_stop();