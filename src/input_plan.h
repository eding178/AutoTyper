#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace typer {
enum class Kind { Unicode, Enter, Tab };
struct Stroke {
    Kind kind = Kind::Unicode;
    std::uint16_t first = 0;
    std::uint16_t second = 0;
    unsigned units = 1;
};
struct Plan {
    std::vector<Stroke> strokes;
    std::string error;
    std::size_t errorOffset = 0;
    explicit operator bool() const { return error.empty(); }
};

// UTF-16 is explicit here so these rules can be tested on non-Windows hosts.
inline Plan makePlan(const std::u16string& text) {
    Plan result;
    result.strokes.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const std::uint16_t c = text[i];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            result.strokes.push_back({Kind::Enter, 0, 0, 1});
        } else if (c == '\t') {
            result.strokes.push_back({Kind::Tab, 0, 0, 1});
        } else if (c < 0x20 || (c >= 0x7f && c <= 0x9f)) {
            result.error = "control";
            result.errorOffset = i;
            result.strokes.clear();
            return result;
        } else if (c >= 0xd800 && c <= 0xdbff) {
            if (i + 1 >= text.size() || text[i + 1] < 0xdc00 || text[i + 1] > 0xdfff) {
                result.error = "surrogate";
                result.errorOffset = i;
                result.strokes.clear();
                return result;
            }
            result.strokes.push_back({Kind::Unicode, c,
                static_cast<std::uint16_t>(text[i + 1]), 2});
            ++i;
        } else if (c >= 0xdc00 && c <= 0xdfff) {
            result.error = "surrogate";
            result.errorOffset = i;
            result.strokes.clear();
            return result;
        } else {
            result.strokes.push_back({Kind::Unicode, c, 0, 1});
        }
    }
    return result;
}
} // namespace typer
