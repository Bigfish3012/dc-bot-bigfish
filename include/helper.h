#ifndef HELPER_H_
#define HELPER_H_

#include <dpp/dpp.h>
#include "runtime_limits.h"
#include "media.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <codecvt>
#include <locale>
#include <stdexcept>
#include <utility>
using namespace std;


bool ends_with(const std::string& str, const std::string& suffix);
// libcurl must be initialized before any bot/worker threads start.

//check api keys and bot token
bool load_config(const std::string& filename, std::string& bot_token,std::string& openai_key,std::string& bing_key);
//txt file
std::string download_file_content(const std::string& file_url,
                                  std::size_t maximum = limits::attachment_bytes);



//OPENAI API
std::string process_openai_response(
    const std::vector<std::pair<std::string, std::string>>& history,
    const std::string& user_message,
    const std::string& openai_api_key,
    const std::vector<InputImage>& images = {});
std::string call_gpt4_api(const std::string& api_key, const nlohmann::json& request_json);
std::string parse_chat_response(const std::string& api_response);
ImageCommand infer_image_command(const std::string& api_key, const std::string& text, bool has_images);
/**
 * Call the OpenAI Moderation API to check if content is flagged.
 * @param api_key Your OpenAI API Key (Bearer token).
 * @param content The text to be moderated.
 * @return JSON string from the OpenAI Moderation endpoint, or empty string on error.
 */
std::string call_openai_moderation_api(const std::string& api_key, const std::string& content);

//PDF
std::string extract_text_from_pdf(const std::string& pdf_content);
std::string ensure_valid_utf8(const std::string& input);

//`.py` `.c` `.cpp` `.java` `.cs` `.js` files
std::string handle_code_file(const std::string& file_url, const std::string& file_extension, const dpp::message_create_t& event);
enum class ModerationResult { allowed, flagged, error };
ModerationResult parse_moderation_response(const std::string& response);
ModerationResult check_content(const std::string& api_key, const std::string& content,
                               const std::vector<InputImage>& images = {});
std::string generate_image(const std::string& api_key, const std::string& prompt,
                           const std::vector<InputImage>& images = {});
std::string get_file_extension(const std::string& filename);
#endif
