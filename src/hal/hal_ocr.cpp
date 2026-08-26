// hal_ocr.cpp — On-Device OCR SKELETON.
//
// =====================================================================
//  WHAT TO ADD (YAGNI — skip unless needed)
// =====================================================================
//
//  ponytail: this is heavy. The Pi 5 has Tesseract
//  (`apt install tesseract-ocr libtesseract-dev`); the ESP32 needs a
//  far smaller lib (Tess4J Offspring, or `lcdUI_resurrect` library)
//  that runs on RAM-tight microcontrollers.
//
//  ── Pi 5 path ──────────────────────────────────────────────────────
//      1. Capture a still frame via the existing camera subsystem
//         (`raspistill -o frame.bmp`).
//      2. `tesseract frame.bmp stdout` shell-out from the OCR task.
//      3. Return the text alongside confidence >= 60.
//
//  ── ESP32 path ──────────────────────────────────────────────────────
//      Vendor `m5stack/TFLiteMicro_OCR` (or equivalent) into
//      `third_party/`. Models are ~400 KB; PsRAM-friendly.
//
//  ── ALTERNATIVES ───────────────────────────────────────────────────
//
//  If the operator only views documentation/screenshots on a
//  separate laptop, drop this HAL and add a key-bind in `app_help`
//  that hits the laptop OCR via `hal_serial_term::cmd_ocr`. That's
//  50 lines instead of a full on-device stack.
//
//  =====================================================================

#include "hal_ocr.h"
#include <cstdio>

bool hal_ocr_run(const char *path, char *out, size_t out_len) {
    if (!path || !out) return false;
#ifdef VOIDOS_RPI5
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "tesseract '%s' stdout 2>/dev/null", path);
    FILE *p = popen(cmd, "r");
    if (!p) return false;
    size_t n = std::fread(out, 1, out_len - 1, p);
    pclose(p);
    out[n] = 0;
    return n > 0;
#else
    (void)out; (void)out_len;
    return false;
#endif
}
