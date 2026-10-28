#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace discord_chunk_detail {
struct codepoint {
    std::uint32_t value;
    std::size_t bytes;
};
// The Responses parser supplies valid UTF-8.
inline codepoint read_codepoint(std::string_view text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    const std::size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    std::uint32_t value = first & (bytes == 1 ? 0x7f : bytes == 2 ? 0x1f : bytes == 3 ? 0x0f : 0x07);
    for (std::size_t i = 1; i < bytes; ++i)
        value = (value << 6) | (static_cast<unsigned char>(text[offset + i]) & 0x3f);
    return {value, bytes};
}
inline bool is_whitespace(std::uint32_t value) {
    return (value >= 0x09 && value <= 0x0d) || value == 0x20 || value == 0x85 ||
           value == 0xa0 || value == 0x1680 || (value >= 0x2000 && value <= 0x200a) ||
           value == 0x2028 || value == 0x2029 || value == 0x202f || value == 0x205f ||
           value == 0x3000;
}
inline bool has_content(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto point = read_codepoint(text, i);
        if (!is_whitespace(point.value)) return true;
        i += point.bytes;
    }
    return false;
}
} // namespace discord_chunk_detail

// BMP code points count as one unit; supplementary code points count as two.
// Prefer paragraph, line, then whitespace boundaries in the latter half.
// Whitespace-only chunks are omitted. Markdown fences are not rewritten.
inline std::vector<std::string> split_discord_reply(std::string_view text,
                                                   std::size_t limit = 1900) {
    if (!limit) throw std::invalid_argument("Discord chunk limit must be positive");
    std::vector<std::string> chunks;
    const std::size_t preferred_minimum = limit / 2 + limit % 2;
    for (std::size_t start = 0; start < text.size();) {
        std::size_t end = start, units = 0;
        std::size_t paragraph_end = start, line_end = start, whitespace_end = start;
        while (end < text.size()) {
            const auto point = discord_chunk_detail::read_codepoint(text, end);
            const std::size_t point_units = point.value > 0xffff ? 2 : 1;
            if (point_units > limit - units) break;
            units += point_units;
            end += point.bytes;
            if (units >= preferred_minimum) {
                if (discord_chunk_detail::is_whitespace(point.value)) whitespace_end = end;
                if (point.value == '\n') {
                    line_end = end;
                    if ((end >= 2 && text[end - 2] == '\n') ||
                        (end >= 4 && text.substr(end - 4, 4) == "\r\n\r\n"))
                        paragraph_end = end;
                }
            }
        }
        if (end == start)
            throw std::invalid_argument("Discord chunk limit cannot fit a Unicode code point");
        if (end != text.size())
            end = paragraph_end != start ? paragraph_end :
                  line_end != start ? line_end :
                  whitespace_end != start ? whitespace_end : end;
        const auto chunk = text.substr(start, end - start);
        if (discord_chunk_detail::has_content(chunk)) chunks.emplace_back(chunk);
        start = end;
    }
    return chunks;
}
