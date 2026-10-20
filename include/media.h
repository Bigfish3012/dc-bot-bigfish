#ifndef MEDIA_H
#define MEDIA_H
#include "bot_error.h"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
struct InputImage { std::string data_url; };
struct ImageCommand { bool requested = false; bool edit_only = false; std::string prompt; };
bool is_image_extension(const std::string& extension);
std::string detect_image_mime(std::string_view bytes);
std::string base64_encode(std::string_view bytes);
std::string base64_decode(std::string_view encoded, std::size_t maximum);
InputImage make_input_image(std::string_view bytes);
ImageCommand parse_image_command(const std::string& text);
ImageCommand parse_image_intent(const std::string& response, const std::string& prompt);
nlohmann::json multimodal_content(const std::string& text, const std::vector<InputImage>& images);
nlohmann::json image_request(const std::string& model, const std::string& prompt, const std::vector<InputImage>& images);
std::string parse_image_response(const std::string& response);
std::string http_error_reason(long status, const std::string& body, bool attachment);
#endif
