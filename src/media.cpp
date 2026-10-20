#include "../include/media.h"
#include "../include/runtime_limits.h"
#include <algorithm>
#include <cctype>
#include <limits>

bool is_image_extension(const std::string& extension) {
    return extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "webp";
}
std::string detect_image_mime(std::string_view bytes) {
    if (bytes.size() >= 8 && bytes.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8)) return "image/png";
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 && static_cast<unsigned char>(bytes[2]) == 0xff) return "image/jpeg";
    if (bytes.size() >= 12 && bytes.substr(0,4) == "RIFF" && bytes.substr(8,4) == "WEBP") return "image/webp";
    return {};
}
std::string base64_encode(std::string_view bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (bytes.size() > limits::image_input_bytes) throw ChatError("Image exceeds input limit", "图片超过 8 MiB 限制。");
    std::string result;
    result.reserve((bytes.size()+2)/3*4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = i+1 < bytes.size() ? static_cast<unsigned char>(bytes[i+1]) : 0;
        const auto c = i+2 < bytes.size() ? static_cast<unsigned char>(bytes[i+2]) : 0;
        result += alphabet[a >> 2];
        result += alphabet[((a & 3) << 4) | (b >> 4)];
        result += i+1 < bytes.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        result += i+2 < bytes.size() ? alphabet[c & 63] : '=';
    }
    return result;
}
std::string base64_decode(std::string_view encoded, std::size_t maximum) {
    auto fail = []() -> void { throw ChatError("Invalid generated image encoding", "图片服务返回的图片编码无效，无法发送。"); };
    if (encoded.empty() || encoded.size()%4 || maximum > std::numeric_limits<std::size_t>::max()/4-2 ||
        encoded.size() > ((maximum+2)/3)*4) fail();
    auto value = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c-'A';
        if (c >= 'a' && c <= 'z') return c-'a'+26;
        if (c >= '0' && c <= '9') return c-'0'+52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string result;
    result.reserve(std::min(maximum,encoded.size()/4*3));
    for (std::size_t i = 0; i < encoded.size(); i += 4) {
        const int a=value(encoded[i]), b=value(encoded[i+1]);
        const bool p2=encoded[i+2]=='=', p3=encoded[i+3]=='=';
        const int c=p2 ? 0 : value(encoded[i+2]), d=p3 ? 0 : value(encoded[i+3]);
        if (a<0 || b<0 || c<0 || d<0 || (p2 && !p3) ||
            ((p2 || p3) && i+4 != encoded.size()) || (p2 && (b & 15)) || (!p2 && p3 && (c & 3))) fail();
        const auto count = static_cast<std::size_t>(1 + !p2 + !p3);
        if (count > maximum-result.size()) throw ChatError("Generated image too large", "生成图片超过 8 MiB 发送限制，请尝试更简单的图片。");
        result += static_cast<char>((a<<2) | (b>>4));
        if (!p2) result += static_cast<char>((b<<4) | (c>>2));
        if (!p3) result += static_cast<char>((c<<6) | d);
    }
    return result;
}
InputImage make_input_image(std::string_view bytes) {
    const auto mime=detect_image_mime(bytes);
    if (mime.empty()) throw ChatError("Unsupported or corrupt image signature", "图片内容不是有效的 PNG、JPEG 或 WebP，请重新导出后上传。");
    return {"data:" + mime + ";base64," + base64_encode(bytes)};
}
ImageCommand parse_image_command(const std::string& text) {
    for (const auto& prefix : {std::string("!image"),std::string("!edit"),std::string("画图"),std::string("生成图片"),std::string("修改图片")}) {
        if (text.compare(0,prefix.size(),prefix) != 0) continue;
        if (prefix[0]=='!' && text.size()>prefix.size() &&
            !std::isspace(static_cast<unsigned char>(text[prefix.size()])) && text[prefix.size()]!=':') continue;
        auto prompt=text.substr(prefix.size());
        for (;;) {
            if (!prompt.empty() && (std::isspace(static_cast<unsigned char>(prompt.front())) || prompt.front()==':')) prompt.erase(0,1);
            else if (prompt.compare(0,std::string("：").size(),"：")==0) prompt.erase(0,std::string("：").size());
            else break;
        }
        return {true,prefix=="!edit" || prefix=="修改图片",std::move(prompt)};
    }
    return {};
}
nlohmann::json multimodal_content(const std::string& text, const std::vector<InputImage>& images) {
    if (images.empty()) return text;
    auto parts=nlohmann::json::array();
    if (!text.empty()) parts.push_back({{"type","text"},{"text",text}});
    for (const auto& image:images) parts.push_back({{"type","image_url"},{"image_url",{{"url",image.data_url}}}});
    return parts;
}
nlohmann::json image_request(const std::string& model, const std::string& prompt, const std::vector<InputImage>& images) {
    if (prompt.empty()) throw ChatError("Empty image prompt", "请提供图片描述，例如：!image 一只骑自行车的鹅。");
    if (prompt.size()>limits::image_prompt_bytes) throw ChatError("Image prompt too long", "图片描述超过 32000 字节，请缩短描述。");
    nlohmann::json request={{"model",model},{"prompt",prompt},{"n",1},{"size","1024x1024"},{"quality","medium"},{"output_format","png"}};
    if (!images.empty()) {
        request["images"]=nlohmann::json::array();
        for (const auto& image:images) request["images"].push_back({{"image_url",image.data_url}});
    }
    return request;
}
std::string parse_image_response(const std::string& response) {
    try {
        const auto parsed=nlohmann::json::parse(response);
        if (!parsed.contains("data") || !parsed["data"].is_array() || parsed["data"].empty() ||
            !parsed["data"][0].contains("b64_json") || !parsed["data"][0]["b64_json"].is_string()) {
            throw ChatError("Image API returned no image", "图片服务没有返回图片，可能未能完成生成，请重试或调整描述。");
        }
        auto bytes=base64_decode(parsed["data"][0]["b64_json"].get<std::string>(),limits::generated_image_bytes);
        if (detect_image_mime(bytes)!="image/png") throw ChatError("Image API returned an unexpected image format", "图片服务返回了非 PNG 数据，无法作为图片发送。");
        return bytes;
    } catch (const nlohmann::json::exception&) {
        throw ChatError("Malformed Image API response", "图片服务返回的数据格式错误，无法读取图片。");
    }
}
std::string http_error_reason(long status, const std::string& body, bool attachment) {
    const auto suffix="（HTTP " + std::to_string(status) + "）。";
    if (attachment) {
        if (status==403 || status==404) return "附件链接已过期或无法访问，请重新上传"+suffix;
        return "附件服务器拒绝下载"+suffix;
    }
    std::string code;
    const auto parsed=nlohmann::json::parse(body,nullptr,false);
    if (parsed.is_object() && parsed.contains("error") && parsed["error"].is_object() &&
        parsed["error"].contains("code") && parsed["error"]["code"].is_string()) code=parsed["error"]["code"].get<std::string>();
    if (code=="insufficient_quota" || code=="billing_hard_limit_reached") return "OpenAI 余额或项目额度不足，请管理员检查 API 账单"+suffix;
    if (code=="content_policy_violation" || code=="moderation_blocked" || code=="safety_system") return "OpenAI 未通过该图片或描述的安全检查，请调整内容"+suffix;
    if (code=="model_not_found") return "配置的模型不存在，或 API Key 无权调用该模型"+suffix;
    if (code=="invalid_image" || code=="invalid_image_format" || code=="image_parse_error" || code=="unsupported_image") return "OpenAI 无法解析图片，请重新导出为 PNG、JPEG 或 WebP"+suffix;
    if (code=="image_too_large") return "图片尺寸或体积超出 OpenAI 接口要求，请缩小后重试"+suffix;
    if (status==401) return "OpenAI API Key 无效或已失效，请管理员检查密钥"+suffix;
    if (status==403) return "OpenAI 拒绝访问，请检查模型权限、项目权限或组织验证"+suffix;
    if (status==404) return "OpenAI 接口或模型不可用，请管理员检查配置"+suffix;
    if (status==429) return "OpenAI 请求频率或用量超限，请稍后重试"+suffix;
    if (status>=500) return "OpenAI 服务端暂时异常，请稍后重试"+suffix;
    if (status==400 || status==422) return "OpenAI 拒绝了请求参数、图片或描述，请检查格式并缩短描述后重试"+suffix;
    if (status==413) return "请求内容超过 OpenAI 接口允许的大小，请减少图片或文本"+suffix;
    return "OpenAI 请求失败，请管理员检查服务状态"+suffix;
}

// Interpret only the constrained routing result; keep the original user prompt intact.
ImageCommand parse_image_intent(const std::string& response, const std::string& prompt) {
    try {
        const auto result=nlohmann::json::parse(response);
        if (result.is_object() && result.size()==1 && result.contains("action") && result["action"].is_string()) {
            const auto action=result["action"].get<std::string>();
            if (action=="chat") return {};
            if (action=="generate" || action=="edit") return {true,action=="edit",prompt};
        }
    } catch (const nlohmann::json::exception&) {}
    throw ChatError("Invalid image intent result","无法判断是否需要生成或编辑图片：意图识别返回格式异常。请使用 !image 描述，或附图使用 !edit 修改要求。看图问答请稍后重试。");
}
