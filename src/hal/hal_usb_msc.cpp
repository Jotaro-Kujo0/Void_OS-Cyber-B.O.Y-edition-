// hal_usb_msc.cpp — USB Mass Storage gadget (Raspberry Pi 5).
//
// Presents the loot directory as a USB-attached drive so an operator can
// plug the device into a laptop and pull captured files. The wirless side
// does the field work; this module is only exercised when the operator
// explicitly mounts the device.
//
// Implementation (Linux / configfs / libcomposite):
//   1. Build a fresh FAT32 backing image and copy the loot root into it.
//   2. Create a configfs gadget with a mass_storage function backed by
//      that image.
//   3. Bind the first available UDC to present it to the host.
//   4. On stop: unbind the UDC and remove the gadget hierarchy.
//
// Requires on the Pi 5 (see /boot/firmware/config.txt):
//     dtoverlay=dwc2
//     dr_mode=otg
//
// ESP32 target is a no-op stub (no configfs).

#include "hal_usb_msc.h"
#include "hal_loot.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef VOIDOS_RPI5
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <dirent.h>
#include <errno.h>
#endif

// Pure computation, target-agnostic so it can be unit-tested with
// captured du/find output.
UsbMscStats hal_usb_msc_stats_from(uint32_t capacity_kb, uint32_t used_kb,
                                    uint32_t files_count, bool active) {
    UsbMscStats s{};
    s.active      = active;
    s.capacity_kb = capacity_kb;
    s.files_count = files_count;
    // Remaining space on the USB image, clamped to zero.
    s.free_kb = (capacity_kb > used_kb) ? (capacity_kb - used_kb) : 0;
    return s;
}

#ifdef VOIDOS_RPI5

// ── Paths ────────────────────────────────────────────────────────────────

#define MSC_LOOT_DIR     VOIDOS_LOOT_ROOT
#define MSC_GADGET_BASE  "/sys/kernel/config/usb_gadget/voidos_msc"
#define MSC_IMG_PATH     "/var/lib/void-os/msc.img"
#define MSC_IMG_MNT      "/var/lib/void-os/msc_mount"
#define MSC_IMG_SIZE     (8 * 1024 * 1024)   // 8 MB backing image

static bool _msc_on = false;

// ── Small filesystem helpers ─────────────────────────────────────────────

static void sysfs_write(const char *path, const char *val) {
    int fd = ::open(path, O_WRONLY);
    if (fd < 0) return;
    ssize_t n = ::write(fd, val, std::strlen(val));
    (void)n;
    ::close(fd);
}

static void rm_rf(const char *path) {
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    std::system(cmd);
}

// `rmdir` won't recurse, so clear a gadget tree with rm -rf before
// recreating it; configfs handles removal fine once the UDC is detached.
static void clear_gadget() {
    sysfs_write(MSC_GADGET_BASE "/UDC", "");   // unbind first (best effort)
    rm_rf(MSC_GADGET_BASE);
    rm_rf(MSC_IMG_MNT);
    rm_rf(MSC_IMG_PATH);
}

// Build + populate the backing FAT image with the current loot contents.
static bool build_backing_image() {
    ::mkdir("/var/lib/void-os",  0755);
    ::mkdir(MSC_LOOT_DIR,        0755);
    ::mkdir(MSC_IMG_MNT,         0755);

    // 1. Create and format a fresh image.
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "dd if=/dev/zero of=%s bs=1M count=%d 2>/dev/null && "
                  "mkfs.vfat -n VOIDOS -F 32 %s 2>/dev/null",
                  MSC_IMG_PATH, MSC_IMG_SIZE / (1024 * 1024), MSC_IMG_PATH);
    if (std::system(cmd) != 0) return false;

    // 2. Mount it.
    std::snprintf(cmd, sizeof(cmd),
                  "mount -o loop,offset=0 %s %s 2>/dev/null",
                  MSC_IMG_PATH, MSC_IMG_MNT);
    if (std::system(cmd) != 0) return false;

    // 3. Copy loot into it, sync, unmount.
    std::snprintf(cmd, sizeof(cmd),
                  "cp -a %s/. %s/ 2>/dev/null", MSC_LOOT_DIR, MSC_IMG_MNT);
    std::system(cmd);
    std::system("sync");
    ::umount(MSC_IMG_MNT);
    return true;
}

// Find the first available UDC controller name.
static bool find_udc(char *out, size_t out_len) {
    FILE *fp = ::popen("ls /sys/class/udc/ 2>/dev/null | head -1", "r");
    if (!fp) return false;
    bool ok = ::fgets(out, (int)out_len, fp) != nullptr;
    ::pclose(fp);
    if (!ok) return false;
    size_t len = std::strlen(out);
    if (len > 0 && out[len - 1] == '\n') out[len - 1] = '\0';
    return out[0] != '\0';
}

bool hal_usb_msc_start() {
    if (_msc_on) return true;

    // Remove any stale gadget/image first so we always surface the
    // current loot contents, then (re)build the backing image.
    clear_gadget();
    if (!build_backing_image()) return false;

    ::mkdir(MSC_GADGET_BASE, 0755);
    sysfs_write(MSC_GADGET_BASE "/idVendor",   "0x1d6b");  // Linux Foundation
    sysfs_write(MSC_GADGET_BASE "/idProduct",  "0x0104");
    sysfs_write(MSC_GADGET_BASE "/bcdUSB",     "0x0200");
    sysfs_write(MSC_GADGET_BASE "/bMaxPacketSize0", "64");

    ::mkdir(MSC_GADGET_BASE "/strings/0x409", 0755);
    sysfs_write(MSC_GADGET_BASE "/strings/0x409/manufacturer", "Void-OS");
    sysfs_write(MSC_GADGET_BASE "/strings/0x409/product", "Loot Drive");
    sysfs_write(MSC_GADGET_BASE "/strings/0x409/serialnumber", "voidos-msc");

    ::mkdir(MSC_GADGET_BASE "/functions/mass_storage.usb0", 0755);
    sysfs_write(MSC_GADGET_BASE "/functions/mass_storage.usb0/lun/file",
                MSC_IMG_PATH);
    sysfs_write(MSC_GADGET_BASE "/functions/mass_storage.usb0/lun/ro", "0");

    ::mkdir(MSC_GADGET_BASE "/configs/c.1", 0755);
    ::mkdir(MSC_GADGET_BASE "/configs/c.1/strings/0x409", 0755);
    sysfs_write(MSC_GADGET_BASE "/configs/c.1/strings/0x409/configuration",
                "Mass Storage");

    // Link the function into the config.
    {
        char link_cmd[256];
        std::snprintf(link_cmd, sizeof(link_cmd),
                      "ln -sfn %s/functions/mass_storage.usb0 "
                      "%s/configs/c.1/mass_storage.usb0",
                      MSC_GADGET_BASE, MSC_GADGET_BASE);
        std::system(link_cmd);
    }

    // Enable the gadget.
    char udc[64];
    if (!find_udc(udc, sizeof(udc))) {
        clear_gadget();
        return false;
    }
    sysfs_write(MSC_GADGET_BASE "/UDC", udc);
    _msc_on = true;
    return true;
}

void hal_usb_msc_stop() {
    if (!_msc_on) return;
    sysfs_write(MSC_GADGET_BASE "/UDC", "");   // detach from host
    clear_gadget();
    _msc_on = false;
}

UsbMscStats hal_usb_msc_stats() {
    // Report capacity in terms of the FAT backing image and the staged
    // loot root. `du -sk` is the same fixed-cost trick used by
    // hal_loot_stats — enumeration here is O(n) the same way.
    uint32_t capacity_kb = 0;
    unsigned long used_kb = 0;
    unsigned long files_count = 0;

    struct stat img{};
    if (::stat(MSC_IMG_PATH, &img) == 0)
        capacity_kb = (uint32_t)(img.st_size / 1024);

    FILE *p = ::popen("du -sk " MSC_LOOT_DIR " 2>/dev/null | awk '{print $1}'", "r");
    if (p) {
        unsigned long used = 0;
        if (std::fscanf(p, "%lu", &used) == 1) used_kb = used;
        ::pclose(p);
    }

    FILE *q = ::popen("find " MSC_LOOT_DIR " -type f 2>/dev/null | wc -l", "r");
    if (q) {
        if (std::fscanf(q, "%lu", &files_count) == 1) { /* no-op */ }
        ::pclose(q);
    }

    return hal_usb_msc_stats_from(capacity_kb, (uint32_t)used_kb,
                                  (uint32_t)files_count, _msc_on);
}

void hal_usb_msc_init() {
    // Delay any side effects to start(); the boot path should stay quiet.
    _msc_on = false;
    clear_gadget();
}

#else   // ── ESP32 stub ──────────────────────────────────────────────────

void hal_usb_msc_init() {}
bool hal_usb_msc_start() { return false; }  // no configfs on ESP32
void hal_usb_msc_stop() {}

UsbMscStats hal_usb_msc_stats() {
    return hal_usb_msc_stats_from(0, 0, 0, false);
}

#endif  // VOIDOS_RPI5