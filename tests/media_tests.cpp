#include "../include/media.h"
#include "../include/runtime_limits.h"
#include <cassert>
#include <iostream>
#include <functional>

void rejects(const std::function<void()>& action) {
    bool caught=false;
    try { action(); } catch (const ChatError& error) { caught=!error.user_message().empty(); }
    assert(caught);
}
int main() {
    const std::string png("\x89PNG\r\n\x1a\n",8);
    assert(base64_encode("f")=="Zg==");
    assert(base64_encode("fo")=="Zm8=");
    assert(base64_encode("foo")=="Zm9v");
    std::string binary;
    for (int i=0;i<256;++i) binary+=static_cast<char>(i);
    assert(base64_decode(base64_encode(binary),256)==binary);
    rejects([] { base64_decode("Zg=",100); });
    rejects([] { base64_decode("Zg==AAAA",100); });
    rejects([] { base64_decode("Zh==",100); });
    rejects([] { base64_decode("!!!!",100); });
    rejects([] { base64_decode("Zm8=",1); });
    assert(is_image_extension("jpeg") && !is_image_extension("gif"));
    assert(detect_image_mime(png)=="image/png");
    assert(detect_image_mime(std::string("\xff\xd8\xff",3))=="image/jpeg");
    assert(detect_image_mime("RIFFxxxxWEBPxxxx")=="image/webp");
    rejects([] { make_input_image("<html>not an image</html>"); });
    const auto image=make_input_image(png);
    assert(image.data_url=="data:image/png;base64,"+base64_encode(png));
    assert(multimodal_content("plain",{}).is_string());
    const auto parts=multimodal_content("question",{image});
    assert(parts.size()==2 && parts[0]["type"]=="text" && parts[1]["type"]=="image_url");
    assert(parts[1]["image_url"]["url"]==image.data_url);
    assert(parse_image_command("!image cat").requested);
    assert(parse_image_command("!image: cat").prompt=="cat");
    assert(parse_image_command("生成图片：蓝色圆形").prompt=="蓝色圆形");
    assert(parse_image_command("!edit red").edit_only);
    assert(!parse_image_command("!imagery cat").requested);
    assert(!parse_image_command("what is in the image?").requested);
    const std::string original="保留鹅和自行车，只将背景改成日落海边。";
    assert(!parse_image_intent(R"({"action":"chat"})",original).requested);
    const auto natural_edit=parse_image_intent(R"({"action":"edit"})",original);
    assert(natural_edit.requested && natural_edit.edit_only && natural_edit.prompt==original);
    const auto natural_generation=parse_image_intent(R"({"action":"generate"})","画一只猫");
    assert(natural_generation.requested && !natural_generation.edit_only);
    rejects([] { parse_image_intent("not json","test"); });
    rejects([] { parse_image_intent(R"({"action":"unknown"})","test"); });
    rejects([] { parse_image_intent(R"({"action":null})","test"); });
    rejects([] { parse_image_intent(R"({"action":"edit","prompt":"replaced"})","test"); });
    const auto generation=image_request("test-model","cat",{});
    assert(!generation.contains("images") && generation["n"]==1 && generation["output_format"]=="png");
    const auto edit=image_request("test-model","red",{image});
    assert(edit["images"][0]["image_url"]==image.data_url);
    rejects([] { image_request("test-model","",{}); });
    assert(parse_image_response(nlohmann::json{{"data",nlohmann::json::array({{{"b64_json",base64_encode(png)}}})}}.dump())==png);
    rejects([] { parse_image_response("{}"); });
    rejects([] { parse_image_response("not json"); });
    rejects([] { parse_image_response("{\"data\":[{\"b64_json\":\"Zm9v\"}]}"); });
    assert(http_error_reason(401,"{\"error\":{\"message\":\"sk-secret-do-not-echo\"}}",false).find("sk-secret")==std::string::npos);
    assert(http_error_reason(401,"",false).find("Key")!=std::string::npos);
    assert(http_error_reason(403,"",false).find("权限")!=std::string::npos);
    assert(http_error_reason(429,"{\"error\":{\"code\":\"insufficient_quota\"}}",false).find("余额")!=std::string::npos);
    assert(http_error_reason(429,"",false).find("频率")!=std::string::npos);
    assert(http_error_reason(403,"",true).find("重新上传")!=std::string::npos);
    std::cout<<"PASS: image signatures/base64/bounds, multimodal payloads, generation/edit routing, image responses and safe error reasons\n";
}
