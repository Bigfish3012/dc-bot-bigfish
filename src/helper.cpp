#include "../include/helper.h"
#include "../include/runtime_limits.h"
#include "../include/bounded_buffer.h"
#include "../include/text_utils.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace {

using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using CurlHeaders = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

std::string http_request(const std::string& url, const std::string* body,
                         const std::string& api_key, std::size_t maximum,
                         long timeout) {
    CurlHandle curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) return {};
    BoundedBuffer response{{}, maximum};
    CurlHeaders headers(nullptr, curl_slist_free_all);
    if (body) {
        auto add_header = [&](const std::string& value) {
            auto* next = curl_slist_append(headers.get(), value.c_str());
            if (!next) throw std::bad_alloc();
            headers.release();
            headers.reset(next);
        };
        add_header("Content-Type: application/json");
        add_header("Authorization: Bearer " + api_key);
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body->data());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(body->size()));
    }
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, bounded_write);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, limits::connect_seconds);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 32L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maximum));
    const auto result = curl_easy_perform(curl.get());
    long status = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK || status < 200 || status >= 300 || response.exceeded) {
        // Never log credentials, response bodies or signed attachment URLs.
        std::cerr << "[ERROR] HTTP request failed: status=" << status
                  << " curl=" << curl_easy_strerror(result)
                  << " size_limit=" << response.exceeded << '\n';
        return {};
    }
    return std::move(response.data);
}
}

bool ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool load_config(const std::string& filename, std::string& bot_token,
                 std::string& openai_key, std::string& bing_key) {
    try {
        std::ifstream config_file(filename);
        if (!config_file) return false;
        nlohmann::json config;
        config_file >> config;
        bot_token = config.at("BOT_TOKEN").get<std::string>();
        openai_key = config.at("OPENAI_API_KEY").get<std::string>();
        // Bing Search is retired; a Bing key is no longer required.
        bing_key = config.value("BING_API_KEY", std::string{});
        return !bot_token.empty() && !openai_key.empty();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Could not load configuration: " << e.what() << '\n';
        return false;
    }
}

std::string download_file_content(const std::string& file_url, std::size_t maximum) {
    return http_request(file_url, nullptr, {}, std::min(maximum, limits::attachment_bytes), limits::download_seconds);
}

std::string call_gpt4_api(const std::string& api_key, const nlohmann::json& request_json) {
    const auto body = request_json.dump();
    return http_request("https://api.openai.com/v1/chat/completions", &body, api_key,
                        limits::api_response_bytes, limits::chat_seconds);
}

std::string process_openai_response(
    const std::vector<std::pair<std::string, std::string>>& history,
    const std::string& user_message,
    const std::string& openai_api_key) {

    // Construct the OpenAI API request
    nlohmann::json messages = { {{"role", "system"}, {"content", "You are a helpful assistant."}} };
    for (const auto& [user_msg, bot_reply] : history) {
        messages.push_back({ {"role", "user"}, {"content", user_msg} });
        messages.push_back({ {"role", "assistant"}, {"content", bot_reply} });
    }
    messages.push_back({ {"role", "user"}, {"content", user_message} });

    nlohmann::json request_json = {
        {"model", "gpt-6-luna"},
        {"messages", messages},
        {"max_completion_tokens", 4096}
    };

    // Call the OpenAI API
    std::string api_response = call_gpt4_api(openai_api_key, request_json);

    if (api_response.empty()) throw std::runtime_error("Chat API request failed");
    const auto response = nlohmann::json::parse(api_response);
    const auto reply = response.at("choices").at(0).at("message").at("content").get<std::string>();
    if (reply.empty()) throw std::runtime_error("Chat API returned no text");
    return reply;
}

std::string call_openai_moderation_api(const std::string& api_key, const std::string& content) {
    const auto body = nlohmann::json{{"input", content}, {"model", "omni-moderation-latest"}}.dump();
    return http_request("https://api.openai.com/v1/moderations", &body, api_key,
                        limits::moderation_response_bytes, limits::moderation_seconds);
}

ModerationResult parse_moderation_response(const std::string& response) {
    try {
        const auto json = nlohmann::json::parse(response);
        if (json.contains("error") || !json.contains("results") ||
            !json.at("results").is_array() || json.at("results").size() != 1) {
            return ModerationResult::error;
        }
        const auto& result = json.at("results").at(0);
        if (!result.contains("flagged") || !result.at("flagged").is_boolean()) {
            return ModerationResult::error;
        }
        return result.at("flagged").get<bool>() ? ModerationResult::flagged : ModerationResult::allowed;
    } catch (const std::exception&) { return ModerationResult::error; }
}

ModerationResult check_content(const std::string& api_key, const std::string& content) {
    if (content.empty()) return ModerationResult::error;
    return parse_moderation_response(call_openai_moderation_api(api_key, content));
}

std::string ensure_valid_utf8(const std::string& input) {
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring wide_string = converter.from_bytes(input); // Converts to wide string to validate UTF-8
        return converter.to_bytes(wide_string); // Converts back to UTF-8
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Invalid UTF-8 string encountered: " << e.what() << std::endl;
        return ""; // Return an empty string if invalid
    }
}

std::string handle_code_file(const std::string& file_url, const std::string& file_extension,
                             const dpp::message_create_t&) {
    auto content = download_file_content(file_url);
    if (content.empty()) return {};
    return "\n[" + file_extension + "]\n" + content;
}

std::string get_file_extension(const std::string& filename) {
    return lowercase_extension(filename);
}
