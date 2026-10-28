#include "../include/discord_chunks.h"
#include "../include/text_utils.h"
#include <cassert>
#include <iostream>
#include <string>

std::string join(const std::vector<std::string>& chunks) {
    std::string out;
    for (const auto& chunk : chunks) {
        assert(!chunk.empty() && discord_text_length(chunk) <= 1900);
        // Independently reject truncated or malformed UTF-8 at chunk boundaries.
        for (std::size_t i = 0; i < chunk.size();) {
            const auto c = static_cast<unsigned char>(chunk[i]);
            const std::size_t n = c < 0x80 ? 1 : c >= 0xc2 && c <= 0xdf ? 2 :
                                  c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            assert(n && i + n <= chunk.size());
            for (std::size_t j = 1; j < n; ++j)
                assert((static_cast<unsigned char>(chunk[i+j]) & 0xc0) == 0x80);
            i += n;
        }
        out += chunk;
    }
    return out;
}
int main() {
    assert(split_discord_reply("").empty());
    assert(split_discord_reply(std::string(6000, ' ')).empty());
    assert(split_discord_reply(u8"\u3000\u00a0\u2003").empty());
    for (std::size_t n : {1899u, 1900u, 1901u, 25000u}) {
        const std::string text(n, 'a');
        const auto parts = split_discord_reply(text);
        assert(join(parts) == text);
        assert(parts.size() == (n + 1899) / 1900);
    }
    std::string chinese, emoji;
    for (int i = 0; i < 1901; ++i) chinese += u8"\u4e2d";
    for (int i = 0; i < 951; ++i) emoji += u8"\U0001f600";
    assert(split_discord_reply(chinese).size() == 2);
    assert(join(split_discord_reply(chinese)) == chinese);
    assert(split_discord_reply(emoji).size() == 2);
    assert(join(split_discord_reply(emoji)) == emoji);
    const auto mixed = std::string(1899, 'a') + u8"\U0001f600\u4e2d";
    assert(split_discord_reply(mixed).front().size() == 1899);
    assert(join(split_discord_reply(mixed)) == mixed);
    const auto paragraphs = std::string(1000, 'a') + "\n\n" +
        std::string(300, 'b') + "\n" + std::string(300, 'c') + " " + std::string(1000, 'd');
    assert(split_discord_reply(paragraphs).front() == std::string(1000, 'a') + "\n\n");
    assert(join(split_discord_reply(paragraphs)) == paragraphs);
    const auto lines = std::string(1000, 'a') + "\n" + std::string(2000, 'b');
    assert(split_discord_reply(lines).front() == std::string(1000, 'a') + "\n");
    assert(join(split_discord_reply(lines)) == lines);
    const auto crlf = std::string(1000, 'a') + "\r\n\r\n" + std::string(2000, 'b');
    assert(split_discord_reply(crlf).front() == std::string(1000, 'a') + "\r\n\r\n");
    assert(join(split_discord_reply(crlf)) == crlf);
    const auto spaces = std::string(1000, 'a') + " " + std::string(2000, 'b');
    assert(split_discord_reply(spaces).front() == std::string(1000, 'a') + " ");
    assert(join(split_discord_reply(spaces)) == spaces);
    std::string cited;
    for (int i = 0; i < 80; ++i)
        cited += u8"\u6d3b\u52a8\u8bf4\u660e [\u6765\u6e901](https://example.org/event)\n";
    const auto parts = split_discord_reply(cited);
    assert(parts.size() > 1 && join(parts) == cited);
    bool rejected = false;
    try { (void)split_discord_reply("a", 0); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "PASS: Discord chunk boundaries, UTF-8, Chinese, emoji, paragraphs, and citations\n";
}
