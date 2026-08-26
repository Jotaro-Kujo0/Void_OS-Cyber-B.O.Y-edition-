// hal_usb_pd.cpp — USB-PD role swap SKELETON.
//
// =====================================================================
//  IMPLEMENTATION GUIDE
// =====================================================================
//
//  ── Hardware wiring ───────────────────────────────────────────────
//      * Pi 5 has a USB-C PD controller (FUSB302 or the Broadcom
//        VLI805) exposed via the kernel `tcpci` driver.
//      * Switch role via `sysfs`:
//          echo "source" > /sys/class/usb_role/.../role
//
//      (Cable orientation info: subsystem reports what the host
//       does; the policy database under /etc/usb-role chooses
//       default behaviour.)
//
//  ── Real impl ──────────────────────────────────────────────────────────
//      1. On src role requested, write `"dfp"` to the role file.
//      2. On the device wire, this triggers the PD controller to
//         send Source_Capabilities (5V@3A, 9V@3A, 15V@3A, 20V@5A).
//      3. Wait for Accept message and shift power delivery.
//      4. Hold for N seconds; auto-revert to UFP/sink to avoid
//         self-powering.
//
//  =====================================================================

#include "hal_usb_pd.h"
#include <cstdio>
#include <cstring>
#ifdef VOIDOS_RPI5
#include <cstdlib>
#endif

static PdRole _role = PD_ROLE_SINK;

void hal_usb_pd_init(void) {
#ifdef VOIDOS_RPI5
    // Sample sink detection: `system("cat /sys/class/usb_role/.../role")` — log to hal_storage.
    std::system("cat /sys/class/usb_role/*/role 2>/dev/null > /tmp/role.log");
#endif
}

void hal_usb_pd_set_role(PdRole r) {
    if (_role == r) return;
    _role = r;
#ifdef VOIDOS_RPI5
    if (r == PD_ROLE_SOURCE) {
        std::system("for f in /sys/class/usb_role/*/role; do echo dfp > $f; done");
    } else {
        std::system("for f in /sys/class/usb_role/*/role; do echo ufp > $f; done");
    }
#endif
    (void)r;
}

PdRole hal_usb_pd_get_role(void) { return _role; }
