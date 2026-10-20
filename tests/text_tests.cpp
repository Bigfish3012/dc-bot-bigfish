#include "../include/text_utils.h"
#include <cassert>
#include <iostream>

int main() {
    assert(discord_text_length("") == 0);
    assert(discord_text_length(u8"中文abc") == 5);
    assert(discord_text_length(u8"中😀文") == 4);
    std::string chinese;
    for (int i = 0; i < 1900; ++i) chinese += u8"中";
    assert(chinese.size() == 5700);
    assert(!reply_needs_attachment(chinese));
    assert(reply_needs_attachment(chinese + u8"文"));
    assert(!reply_needs_attachment(std::string(1900, 'a')));
    assert(reply_needs_attachment(std::string(1901, 'a')));
    std::string emoji;
    for (int i = 0; i < 950; ++i) emoji += u8"😀";
    assert(!reply_needs_attachment(emoji));
    assert(reply_needs_attachment(emoji + "a"));
    assert(lowercase_extension("REPORT.PDF") == "pdf");
    assert(lowercase_extension("a.TXT") == "txt");
    assert(lowercase_extension("source.CpP") == "cpp");
    assert(lowercase_extension("archive.tar.GZ") == "gz");
    assert(lowercase_extension("no_extension").empty());
    assert(lowercase_extension("trailing.").empty());
    assert(history_user_text("question") == "question");
    assert(!history_user_text("").empty());
    const std::string attachment = "attachment-only-secret";
    assert(history_user_text("question").find(attachment) == std::string::npos);
    assert(history_user_text("").find(attachment) == std::string::npos);
    std::cout << "PASS: ASCII/Chinese/emoji reply boundaries, case-insensitive extensions, history user text\n";
}
