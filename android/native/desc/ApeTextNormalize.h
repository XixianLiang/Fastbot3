/**
 * @file ApeTextNormalize.h
 *
 * Lightweight string normalization for UI snapshot fields: strip ASCII double-quote characters and optionally
 * bound visible text length in UTF-8 codepoints so hashing / naming stays compact and stable.
 *
 * @authors Zhao Zhang
 */

#ifndef FASTBOTX_DESC_APE_TEXT_NORMALIZE_H_
#define FASTBOTX_DESC_APE_TEXT_NORMALIZE_H_

#include <cstddef>
#include <cstring>
#include <string>

namespace fastbotx {
/** Helpers shared by GUI tree and naming code paths (`normalizeTextFor*` entry points below). */
namespace ape_text {

/** Returns a copy of `input` without `"` characters; null `input` yields an empty string. */
inline std::string removeDoubleQuotes(const char *input) {
    if (!input) {
        return std::string();
    }
    const size_t n = std::strlen(input);
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const char c = input[i];
        if (c != '"') {
            out.push_back(c);
        }
    }
    return out;
}

/**
 * Truncates at a maximum number of Unicode scalar values (UTF-8 codepoints), not bytes.
 * Walks valid leading bytes; if a sequence would run past `s.size()`, the last byte is advanced one step
 * to avoid hanging on truncated input.
 */
inline std::string truncateUtf8Codepoints(const std::string &s, size_t maxCodepoints) {
    if (s.empty() || maxCodepoints == 0) {
        return std::string();
    }
    size_t i = 0;
    size_t cp = 0;
    while (i < s.size() && cp < maxCodepoints) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t step = 1;
        if ((c & 0x80u) == 0x00u) {
            step = 1;
        } else if ((c & 0xe0u) == 0xc0u) {
            step = 2;
        } else if ((c & 0xf0u) == 0xe0u) {
            step = 3;
        } else if ((c & 0xf8u) == 0xf0u) {
            step = 4;
        }
        if (i + step > s.size()) {
            step = 1;
        }
        i += step;
        ++cp;
    }
    return s.substr(0, i);
}

/** Primary widget `text`: quotes removed, then at most 8 codepoints (compact key for state / tree layers). */
inline std::string normalizeTextForApe(const char *input) {
    return truncateUtf8Codepoints(removeDoubleQuotes(input), 8);
}

/** Content-description field: strip quotes only (full length retained for longer accessibility strings). */
inline std::string normalizeContentDescForApe(const char *input) {
    return removeDoubleQuotes(input);
}

} // namespace ape_text
} // namespace fastbotx

#endif
