// hal_llm.cpp — LLM POST SKELETON.
//
// =====================================================================
//  IMPLEMENTATION STEPS
// =====================================================================
//
//  ponytail: a single POST to a chat completions endpoint. Build the
//  request body and parse the response.
//
//  ── OpenAI-compatible POST ────────────────────────────────────────────
//
//  Endpoint: https://api.openai.com/v1/chat/completions
//  Auth:     Authorization: Bearer $OPENAI_API_KEY
//  Body:     {"model": "gpt-4o-mini",
//             "messages": [{"role":"system","content":"..."},
//                           {"role":"user","content":"<input>"}],
//             "max_tokens": 200}
//
//  ── Anthropic ────────────────────────────────────────────────────────
//
//  Endpoint: https://api.anthropic.com/v1/messages
//  Auth:     x-api-key: $ANTHROPIC_API_KEY
//  Body:     {"model":"claude-3-5-sonnet",
//             "max_tokens": 200,
//             "messages":[{"role":"user","content":"..."}]}
//
//  ── Implementation outline ─────────────────────────────────────────
//
//  1. Store the keys in `hal_storage` obfuscated (XOR with a per-
//     engagement salt kept in hal_settings). Skeleton: just
//     `hal_storage_get_str("llm_key", ...)` once a key is configured.
//
//  2. Use `app_web.cpp::fetch_start` as the template for the HTTP
//     read loop. Replace the host/port/path with the LLM endpoint
//     and add the `Authorization` header.
//
//  3. Parse the response body for `choices[0].message.content` (or
//     `content[0].text` for Anthropic). Drop into `out`.
//
//  ── Security note ─────────────────────────────────────────────────────
//
//  Captured tokens should NOT travel to a third-party LLM. Default
//  the policy to "no remote LLM unless explicit opt-in": operator
//  sets `hal_storage["llm_off"]` to "0" once they've confirmed the
//  data they're passing is theirs to share.
//
//  =====================================================================

#include "hal_llm.h"
#include "hal/hal_storage.h"
#include <cstdio>
#include <cstring>

bool hal_llm_summarize(const char *input, char *out, size_t out_len) {
    if (!input || !out) return false;
    char off[8]; hal_storage_get_str("llm_off", off, sizeof(off));
    if (off[0] && off[0] != '0') {
        std::snprintf(out, out_len, "(LLM off; refusing to send capture data)");
        return false;
    }
    // Real impl: shell-out to `curl -s https://api.openai.com/...` or
    // call app_web's pipeline with a header set. Skeleton returns a
    // local canned note.
    std::snprintf(out, out_len,
        "LLM scaffold: would post %zu bytes", std::strlen(input));
    return true;
}
