// hal_llm.h — one-shot LLM analysis.
//
// Pass a NTLM hash, hccapx, or NTLM token. Call back returns a short
// text describing likely service / actor / next step.
//
// Uses a configurable provider. Default: OpenAI-compatible POST.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

bool hal_llm_summarize(const char *input,
                       char *out, size_t out_len);
