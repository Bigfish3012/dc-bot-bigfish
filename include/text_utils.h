#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <cstddef>
#include <string>
#include <string_view>

// Input has already been validated as UTF-8. Chinese characters count as one;
// count non-BMP characters as two units to conservatively allow UTF-16 limits.
inline std::size_t discord_text_length(std::string_view text) noexcept {
    std::size_t length = 0;
    for (unsigned char byte : text) {
        if ((byte & 0xC0) != 0x80) length += byte >= 0xF0 ? 2 : 1;
    }
    return length;
}

inline bool reply_needs_attachment(std::string_view reply) noexcept {
    return discord_text_length(reply) > 1900;
}

inline std::string lowercase_extension(const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return {};
    auto extension = filename.substr(dot + 1);
    // Supported extensions are ASCII; avoid locale-dependent case conversion.
    for (char& byte : extension) {
        if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte - 'A' + 'a');
    }
    return extension;
}

inline std::string history_user_text(const std::string& message_content) {
    // Attachment bytes belong only to the current request. Avoid an empty user
    // turn for attachment-only messages without retaining the attachment text.
    return message_content.empty() ? "[用户上传了附件，附件原文仅用于当次请求]" : message_content;
}

#endif
