// hal_markdown.h — Markdown report builder.
//
// Builds Markdown from session data (captures, logins, urls) into
// one deliverable file with sections:
//
//   # Engagement  <name>
//   ## Time       <start_ts> – <end_ts>
//   ## Captures   …
//
// The format is plain text. Reader-territory: stash the file on the
// laptop with `markdown -f html report.md > report.html`.
//
// This skeleton outlines the API; the .cpp file is a minimal buffer
// with no advanced parsing — replace `md_*` ops with one-pass
// generation.

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void md_open(const char *path, const char *title);
void md_h(const char *level, const char *heading);
void md_p(const char *text);
void md_code(const char *code, const char *lang);
bool md_close();
