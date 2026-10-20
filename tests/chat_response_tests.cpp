#include "../include/helper.h"
#include <cassert>
#include <iostream>

nlohmann::json response(const nlohmann::json& content, const std::string& finish) {
    return {{"choices", nlohmann::json::array({{
        {"finish_reason", finish}, {"message", {{"content", content}}}
    }})}};
}

void expect_error(const nlohmann::json& value, const std::string& detail) {
    bool caught = false;
    try { parse_chat_response(value.dump()); }
    catch (const ChatError& error) {
        caught = true;
        assert(std::string(error.what()) == detail);
        assert(!error.user_message().empty());
    }
    assert(caught);
}

int main() {
    assert(parse_chat_response(response("complete HTML", "stop").dump()) == "complete HTML");
    expect_error(response("", "length"), "Chat response reached model output limit");
    expect_error(response("partial HTML", "length"), "Chat response reached model output limit");
    expect_error(response(nullptr, "content_filter"), "Chat response filtered or refused");
    auto refusal = response(nullptr, "stop");
    refusal["choices"][0]["message"]["refusal"] = "refused";
    expect_error(refusal, "Chat response filtered or refused");
    expect_error(response("", "stop"), "Chat API returned no text");
    expect_error(response(nullptr, "stop"), "Chat API returned no text");
    auto nullable_usage = response("OK", "stop");
    nullable_usage["usage"] = nullptr;
    assert(parse_chat_response(nullable_usage.dump()) == "OK");
    nullable_usage["usage"] = {{"completion_tokens_details", nullptr}};
    assert(parse_chat_response(nullable_usage.dump()) == "OK");
    bool transport_error = false;
    try { parse_chat_response(""); }
    catch (const ChatError& error) { transport_error = std::string(error.what()) == "Chat API request failed"; }
    assert(transport_error);
    std::cout << "PASS: complete/empty/null/truncated/filtered/refused responses and nullable usage\n";
}
