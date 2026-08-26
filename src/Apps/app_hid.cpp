// app_hid.cpp — HID keystroke emulation (Raspberry Pi 5)
//
// Sub-modes:
//   0. BLE (BadBLE)    — BLE HID keyboard via btmgmt system() calls
//   1. USB (HID)       — USB HID keyboard via configfs libcomposite
//   2. BLE (PERIPH)    — pair as keyboard to a target device
//   3. APPLE (SPOOF)   — AirPods/Continuity beacon spam via hcitool cmd
//
// Dependencies:
//   - bluez (btmgmt, hcitool — no -dev headers needed)
//   - dtoverlay=dwc2 (USB HID gadget)

#include "app_hid.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../hal/hal_haptic.h"
#include "../hal/hal_mosfet.h"
#include "../config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#ifdef VOIDOS_RPI5
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#endif

//  MENU

static const char *LABELS[] = {
    "BLE   (BadBLE)",     // 0
    "USB   (HID)",        // 1
    "BLE   (PERIPH)",     // 2
    "APPLE (SPOOF)",      // 3
};
static const uint8_t N_ROWS = sizeof(LABELS) / sizeof(LABELS[0]);

//  STATE

static uint8_t  _row    = 0;
static bool     _active = false;
static char     _status[24] = "READY";
static uint32_t _frame  = 0;

// HID payload
#define HID_SCRIPT_MAX 256
static char     _script[HID_SCRIPT_MAX] = "Hello from Void-OS";
static uint16_t _script_len = 0;
static uint16_t _script_pos = 0;
static bool     _armed = false;

// BLE spoof state
static bool     _ble_spoofing = false;
static uint8_t  _apple_beacon[31] = {
    0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19, 0x07, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

//  USB HID GADGET (configfs/libcomposite)

#ifdef VOIDOS_RPI5
static int _hidg_fd = -1;

static bool hidg_setup() {
    if (_hidg_fd >= 0) return true;
    
    // Create gadget directory structure
    const char *base = "/sys/kernel/config/usb_gadget/hid";
    mkdir(base, 0755);
    
    char path[128];
    
    // Vendor/Product
    FILE *f;
    snprintf(path, sizeof(path), "%s/idVendor", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "0x1d6b"); fclose(f); } // Linux Foundation
    snprintf(path, sizeof(path), "%s/idProduct", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "0x0104"); fclose(f); } // Multifunction Composite
    
    // Device descriptors
    snprintf(path, sizeof(path), "%s/bcdDevice", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "0x0100"); fclose(f); }
    snprintf(path, sizeof(path), "%s/bcdUSB", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "0x0200"); fclose(f); }
    
    // Strings
    snprintf(path, sizeof(path), "%s/strings/0x409/manufacturer", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "Void-OS"); fclose(f); }
    snprintf(path, sizeof(path), "%s/strings/0x409/product", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "VOID-OS HID Keyboard"); fclose(f); }
    
    // HID function
    snprintf(path, sizeof(path), "%s/functions/hid.usb0", base);
    mkdir(path, 0755);
    
    snprintf(path, sizeof(path), "%s/functions/hid.usb0/protocol", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "1"); fclose(f); } // Keyboard
    snprintf(path, sizeof(path), "%s/functions/hid.usb0/subclass", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "1"); fclose(f); } // Boot
    snprintf(path, sizeof(path), "%s/functions/hid.usb0/report_length", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "8"); fclose(f); }
    
    // Boot keyboard report descriptor
    static const uint8_t desc[] = {
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
        0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
        0x75, 0x08, 0x81, 0x03, 0x95, 0x05, 0x75, 0x01,
        0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
        0x95, 0x01, 0x75, 0x03, 0x91, 0x03, 0x95, 0x06,
        0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
        0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0
    };
    
    snprintf(path, sizeof(path), "%s/functions/hid.usb0/report_desc", base);
    f = fopen(path, "wb");
    if (f) { fwrite(desc, 1, sizeof(desc), f); fclose(f); }
    
    // Config
    snprintf(path, sizeof(path), "%s/configs/c.1", base);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/configs/c.1/strings/0x409/configuration", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "HID Keyboard"); fclose(f); }
    snprintf(path, sizeof(path), "%s/configs/c.1/MaxPower", base);
    f = fopen(path, "w"); if (f) { fprintf(f, "120"); fclose(f); }
    
    // Link function to config
    snprintf(path, sizeof(path), "%s/configs/c.1/hid.usb0", base);
    symlink("/sys/kernel/config/usb_gadget/hid/functions/hid.usb0", path);
    
    // Enable gadget
    DIR *udc_dir = opendir("/sys/class/udc");
    if (udc_dir) {
        struct dirent *ent;
        while ((ent = readdir(udc_dir)) != nullptr) {
            if (ent->d_name[0] != '.') {
                snprintf(path, sizeof(path), "%s/UDC", base);
                f = fopen(path, "w");
                if (f) { fprintf(f, "%s", ent->d_name); fclose(f); }
                break;
            }
        }
        closedir(udc_dir);
    }
    
    // Open hidg0
    _hidg_fd = open("/dev/hidg0", O_WRONLY);
    return _hidg_fd >= 0;
}

static void hidg_teardown() {
    if (_hidg_fd >= 0) { close(_hidg_fd); _hidg_fd = -1; }
    // Unbind UDC
    char path[128];
    FILE *f;
    snprintf(path, sizeof(path), "/sys/kernel/config/usb_gadget/hid/UDC");
    f = fopen(path, "w"); if (f) { fprintf(f, "\n"); fclose(f); }
}

static void hidg_type_char(char c) {
    if (_hidg_fd < 0) return;
    
    // US QWERTY scan code lookup
    static const uint8_t sc[] = {
        0,0,0,0,0,0,0,0,42,43,44,45,46,47,48,49,
        50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,
        44,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,
        45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,
        61,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
        19,20,21,22,23,24,25,26,27,28,29,57,58,59,60,61
    };
    
    uint8_t report[8] = {0};
    bool shift = false;
    
    if (c >= 'A' && c <= 'Z') { shift = true; c += 32; } // to lowercase
    
    if (c >= ' ' && c <= 'z' && c - ' ' < sizeof(sc)) {
        report[0] = shift ? 0x02 : 0x00; // Left Shift
        report[2] = sc[(uint8_t)(c - ' ')];
    } else if (c == '\n') {
        report[2] = 40; // Enter
    } else if (c == '\t') {
        report[2] = 43; // Tab
    }
    
    write(_hidg_fd, report, 8);
    usleep(1000);
    
    // Release
    memset(report, 0, 8);
    write(_hidg_fd, report, 8);
    usleep(1000);
}

static void hidg_type_string(const char *s) {
    for (const char *p = s; *p; ++p) {
        hidg_type_char(*p);
        usleep(2000); // inter-key delay
    }
}
#endif // VOIDOS_RPI5

//  BLE VIA SYSTEM() CALLS no libbluetooth-dev needed

#ifdef VOIDOS_RPI5
static void ble_spoof_on(const uint8_t *data, uint8_t len) {
    // Disable existing advertising
    system("btmgmt power off");
    usleep(50000);
    system("btmgmt le on");
    usleep(50000);
    system("btmgmt power on");
    usleep(100000);
    
    // Build hcitool cmd for LE Set Advertising Data
    // Opcode: OGF=0x08 OCF=0x0008 => cmd 0x2008
    char cmd[256];
    char hex[64] = {0};
    
    for (uint8_t i = 0; i < len && i < 31; ++i) {
        snprintf(hex + i * 2, 3, "%02x", data[i]);
    }
    // Pad to 31 bytes (62 hex chars)
    for (uint8_t i = len; i < 31; ++i) {
        snprintf(hex + i * 2, 3, "00");
    }
    
    snprintf(cmd, sizeof(cmd), "hcitool cmd 8 8 %s", hex);
    system(cmd);
    
    // Enable advertising
    system("hcitool cmd 8 a 1 0 0 0 0 0 0 0 0 0 0 3 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0");
}

static void ble_spoof_off() {
    system("hcitool cmd 8 9"); // LE Set Advertise Disable
    usleep(50000);
}

static void ble_hid_start() {
    system("btmgmt power off");
    usleep(50000);
    system("btmgmt le on");
    system("btmgmt bredr off");
    system("btmgmt name \"VOID-OS-HID\"");
    system("btmgmt power on");
    usleep(100000);
    system("systemctl start bluetooth");
    usleep(200000);
    // BlueZ handles HID profile automatically via udev rules
    snprintf(_status, sizeof(_status), "BLE HID ON");
}

static void ble_hid_stop() {
    system("systemctl stop bluetooth");
    snprintf(_status, sizeof(_status), "BLE HID OFF");
}
#endif // VOIDOS_RPI5

//  US-QWERTY SCancode Map for ESP32 fallback

static const uint8_t KEYMAP[128] = {
    0,0,0,0,0,0,0,0,42,43,44,45,46,47,48,49,
    50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,
    44,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,
    45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,
    61,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
    19,20,21,22,23,24,25,26,27,28,29,57,58,59,60,61
};

//  LIFECYCLE

void app_hid_init() {
    _row = 0;
    _active = false;
    _armed = false;
    _script_len = (uint16_t)strlen(_script);
    snprintf(_status, sizeof(_status), "READY");
}

void app_hid_tick() {
    _frame++;
    
    if (!_armed || _script_pos >= _script_len) return;
    
#ifdef VOIDOS_RPI5
    if (_row == 1 && _hidg_fd >= 0) {
        // USB HID: type one character per tick
        hidg_type_char(_script[_script_pos]);
        _script_pos++;
        if (_script_pos >= _script_len) {
            _armed = false;
            snprintf(_status, sizeof(_status), "DONE %u", _script_len);
        }
    } else
#endif
    {
        // ESP32 BLE path (stub)
        _script_pos = _script_len;
        _armed = false;
    }
}

void app_hid_suspend() {
    _active = false;
    _armed = false;
    _script_pos = 0;
#ifdef VOIDOS_RPI5
    hidg_teardown();
    ble_spoof_off();
    ble_hid_stop();
#endif
    snprintf(_status, sizeof(_status), "READY");
}

//  EVENT HANDLER

void app_hid_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        _active = false;
        _armed = false;
        _script_pos = 0;
#ifdef VOIDOS_RPI5
        hidg_teardown();
        ble_spoof_off();
        ble_hid_stop();
#endif
        snprintf(_status, sizeof(_status), "READY");
        return;
    }
    
    if (e.type == EVT_POT_CHANGED) {
        _row = (e.data * N_ROWS) / 256;
        if (_row >= N_ROWS) _row = N_ROWS - 1;
        return;
    }
    
    if (e.type != EVT_BTN_A_DOWN) return;
    
    _active = true;
    
    switch (_row) {
        case 0: // BLE BadBLE
#ifdef VOIDOS_RPI5
            ble_hid_start();
#else
            snprintf(_status, sizeof(_status), "BLE STUB");
#endif
            break;
            
        case 1: // USB HID
#ifdef VOIDOS_RPI5
            if (hidg_setup()) {
                snprintf(_status, sizeof(_status), "USB HID OK");
            } else {
                snprintf(_status, sizeof(_status), "USB ERR");
            }
#else
            snprintf(_status, sizeof(_status), "USB STUB");
#endif
            break;
            
        case 2: // BLE PERIPH
            snprintf(_status, sizeof(_status), "BLE PERIPH");
            break;
            
        case 3: // APPLE SPOOF
#ifdef VOIDOS_RPI5
            if (!_ble_spoofing) {
                ble_spoof_on(_apple_beacon, sizeof(_apple_beacon));
                _ble_spoofing = true;
                snprintf(_status, sizeof(_status), "APPLE ON");
            } else {
                ble_spoof_off();
                _ble_spoofing = false;
                snprintf(_status, sizeof(_status), "APPLE OFF");
            }
#else
            snprintf(_status, sizeof(_status), "APPLE STUB");
#endif
            break;
    }
}

//  DRAW

void app_hid_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "HID / KEY", T_FG, T_PANEL, FONT_SM);
    draw_textf(SCR_W - 60, 8, _armed ? T_WARN : T_DIM, T_PANEL, FONT_SM,
               "ARM:%d", _armed);
    
    int row_h = 28;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + (int)i * row_h;
        draw_textf(8, y,
                   (_active ? T_WARN : (i == _row ? T_FG : T_DIM)),
                   T_BG, FONT_SM, "%s %s",
                   (i == _row ? ">" : " "), LABELS[i]);
    }
    
    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "Script: %s", _script);
    
    if (_armed && _script_pos < _script_len) {
        int pct = (_script_pos * 100) / _script_len;
        draw_textf(8, info_y + 14, T_WARN, T_BG, FONT_SM,
                   "TYPING %d%% [%u/%u]", pct, _script_pos, _script_len);
    }
    
    draw_textf(8, SCR_H - 36, _active ? T_WARN : T_DIM, T_BG, FONT_SM,
               "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12,
              "[A] ARM/TOGGLE  [B] BACK  POT=mode",
              T_DIM, T_BG, FONT_SM);
}
