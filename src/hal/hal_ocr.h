// hal_ocr.h — on-device OCR SKELETON.
//
// Camera capture led by the existing apps + Tesseract for text
// recognition. Used to scrape QRs, transcribe sticker passwords, and
// populate Man-in-the-Middle menus live from a screen-capture.

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Run OCR on a saved BMP file.
bool hal_ocr_run(const char *path, char *out, size_t out_len);
