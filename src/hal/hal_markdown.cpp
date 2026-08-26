// hal_markdown.cpp — Markdown report writer.
//
// SKELETON: opens a file, writes headers via printf-style. Real impl
// wants an in-memory builder (so callers can append to sections
// from multiple apps in interleaved order) flushed at close.
//
// =====================================================================
//  BUILD STEPS (real impl)
// =====================================================================
//
//  1. Each app wanting to add a row to the reporting session calls:
//
//       md_p("captured MAC AA:BB:CC:DD:EE:FF (vendor: Apple)");
//
//     These calls accumulate in an internal ring buffer (8 KB).
//
//  2. Section-grouping. Use `md_h("##", "Captures")` followed by
//     `md_p("captured ...")`. The header triggers a checkpoint in
//     the buffer so a future `md_close` flushes a coherent document.
//
//  3. At engagement end, the operator fires the report: calls
//     `md_close()` to flush to `/loot/<UTC>_report.md`.
//
//  4. Format options:
//      * With shell tools: `md2html` (Python), `marked` (Node).
//      * On the device: render to a separate "preview" via the
//        existing HTML strip-mode we already shipped in app_web.
//
//  =====================================================================

#include "hal_markdown.h"
#include <cstdio>
#include <cstring>

#ifdef VOIDOS_RPI5
static FILE *_md_f = nullptr;
#else
#endif

void md_open(const char *path, const char *title) {
#ifdef VOIDOS_RPI5
    _md_f = std::fopen(path, "w");
    if (!_md_f) return;
    std::fprintf(_md_f, "# %s\n\n", title ? title : "void-os session");
#else
    (void)path; (void)title;
#endif
}

void md_h(const char *level, const char *heading) {
#ifdef VOIDOS_RPI5
    if (!_md_f) return;
    std::fprintf(_md_f, "%s %s\n\n", level ? level : "##", heading ? heading : "");
#else
    (void)level; (void)heading;
#endif
}

void md_p(const char *text) {
#ifdef VOIDOS_RPI5
    if (!_md_f) return;
    if (!text) return;
    std::fputs(text, _md_f); std::fputc('\n', _md_f);
#else
    (void)text;
#endif
}

void md_code(const char *code, const char *lang) {
#ifdef VOIDOS_RPI5
    if (!_md_f) return;
    std::fprintf(_md_f, "```%s\n%s\n```\n\n", lang ? lang : "", code ? code : "");
#else
    (void)code; (void)lang;
#endif
}

bool md_close() {
#ifdef VOIDOS_RPI5
    if (!_md_f) return false;
    std::fclose(_md_f); _md_f = nullptr;
    return true;
#else
    return false;
#endif
}
