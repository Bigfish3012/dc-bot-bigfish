#ifndef BOT_ERROR_H
#define BOT_ERROR_H
#include <stdexcept>
#include <string>
#include <utility>
class ChatError : public std::runtime_error {
public:
    ChatError(std::string detail, std::string user_message)
        : std::runtime_error(std::move(detail)), user_message_(std::move(user_message)) {}
    const std::string& user_message() const noexcept { return user_message_; }
private:
    std::string user_message_;
};
#endif
