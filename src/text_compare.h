#pragma once
#include <cstddef>
#include <string>

namespace typer {
struct TextComparison {
    bool equal = false;
    // Lengths and offset count UTF-16 code units after newline normalization.
    std::size_t expectedLength = 0, actualLength = 0, mismatchOffset = 0;
    // One-based positions. A surrogate pair is one column; a tab is one column.
    std::size_t line = 1, column = 1;
};

inline std::u16string normalizeNewlines(const std::u16string& text) {
    std::u16string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u'\r') {
            normalized.push_back(u'\n');
            if (i + 1 < text.size() && text[i + 1] == u'\n') ++i;
        } else {
            normalized.push_back(text[i]);
        }
    }
    return normalized;
}

inline bool isHighSurrogate(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
inline bool isLowSurrogate(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }

inline TextComparison compareText(const std::u16string& expectedText,
                                  const std::u16string& actualText) {
    const auto expected = normalizeNewlines(expectedText);
    const auto actual = normalizeNewlines(actualText);
    TextComparison result;
    result.expectedLength = expected.size();
    result.actualLength = actual.size();
    std::size_t offset = 0;
    while (offset < expected.size() && offset < actual.size() && expected[offset] == actual[offset]) ++offset;
    result.equal = offset == expected.size() && offset == actual.size();
    // A differing low surrogate belongs to the character that starts before it.
    if (!result.equal && offset > 0 && offset < expected.size() &&
        isLowSurrogate(expected[offset]) && isHighSurrogate(expected[offset - 1])) --offset;
    result.mismatchOffset = offset;
    for (std::size_t i = 0; i < offset; ++i) {
        if (expected[i] == u'\n') { ++result.line; result.column = 1; }
        else {
            ++result.column;
            if (isHighSurrogate(expected[i]) && i + 1 < offset && isLowSurrogate(expected[i + 1])) ++i;
        }
    }
    return result;
}
} // namespace typer
