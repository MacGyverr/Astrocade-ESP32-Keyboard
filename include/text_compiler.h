#pragma once
#include <cstddef>
#include <cstdint>

struct TextKey { uint8_t usage; uint8_t modifiers; };
struct TextIssue { uint32_t line; uint32_t column; uint32_t codepoint; };
struct TextValidation {
    std::size_t count = 0;
    uint32_t lines = 0;
    uint32_t errors = 0;
    TextIssue issues[16] = {};
};
using TextMapper = bool (*)(uint32_t, TextKey &);

// No hardware access: validate the whole input before its plan can be submitted.
inline TextValidation compileText(const uint8_t *text, std::size_t length,
                                 TextKey *plan, std::size_t capacity, TextMapper map)
{
    TextValidation result;
    uint32_t line = 1, column = 1;
    bool newline = true;
    std::size_t i = length >= 3 && text[0] == 0xEF && text[1] == 0xBB && text[2] == 0xBF ? 3 : 0;
    auto emit = [&](uint32_t rune) {
        TextKey key{};
        if (!map(rune, key) || result.count >= capacity) {
            if (result.errors < 16) result.issues[result.errors] = {line, column, rune};
            result.errors++;
        } else {
            if (plan) plan[result.count] = key;
            result.count++;
        }
        newline = rune == '\n';
        if (newline) { result.lines++; line++; column = 1; }
        else column++;
    };
    while (i < length) {
        uint32_t rune = text[i++];
        if (rune == '\r') {
            if (i < length && text[i] == '\n') i++;
            rune = '\n';
        } else if (rune >= 0x80) {
            const uint8_t lead = static_cast<uint8_t>(rune);
            unsigned extra = lead >= 0xC2 && lead <= 0xDF ? 1 :
                lead >= 0xE0 && lead <= 0xEF ? 2 : lead >= 0xF0 && lead <= 0xF4 ? 3 : 0;
            rune &= extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07;
            bool valid = extra && i + extra <= length;
            for (unsigned n = 0; valid && n < extra; n++) {
                if ((text[i + n] & 0xC0) != 0x80) valid = false;
                else rune = (rune << 6) | (text[i + n] & 0x3F);
            }
            if (valid) {
                i += extra;
                valid = rune >= (extra == 1 ? 0x80U : extra == 2 ? 0x800U : 0x10000U) &&
                    rune <= 0x10FFFF && !(rune >= 0xD800 && rune <= 0xDFFF);
            }
            if (!valid) rune = 0xFFFD;
        }
        emit(rune);
    }
    if (!newline) emit('\n');
    return result;
}
