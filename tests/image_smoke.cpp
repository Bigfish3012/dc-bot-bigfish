// Manual integration check: calls OpenAI using config.json; never sends Discord messages.
#include "../include/helper.h"
#include <iterator>
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    const auto initialized=curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized!=CURLE_OK) return 2;
    int result=0;
    try {
        std::string token,key,bing;
        if (!load_config("config.json",token,key,bing)) return 2;
        const std::string mode=argv[1];
        if (mode=="intent") {
            struct Case { const char* text; bool attached; bool requested; bool edit; };
            const Case cases[]={
                {"保留原图中的鹅、自行车、构图和卡通手绘风格，只将背景改成日落海边：远处加入平静的海面与海平线，近处是沙滩，不要改变主体。",true,true,true},
                {"把日落背景改为海边",true,true,true},
                {"画一只骑自行车的鹅，发图片给我",false,true,false},
                {"这张图片里有什么？",true,false,false},
                {"OpenAI 的图片编辑 API 怎么调用？",false,false,false},
                {"写一个自包含 HTML，用 SVG 画鹅骑自行车的日落场景",false,false,false},
                {"把‘将背景改成海边’翻译成英文",false,false,false},
                {"把这张照片背景改成海边",false,true,true}
            };
            int index=0;
            for (const auto& test:cases) {
                const auto result=infer_image_command(key,test.text,test.attached);
                if (result.requested!=test.requested || (result.requested && result.edit_only!=test.edit))
                    throw std::runtime_error("Intent classification mismatch");
                std::cout<<"PASS intent case "<<++index<<std::endl;
            }
        } else if (mode=="vision") {
            std::ifstream file("build/local-checks/vision-fixture.png",std::ios::binary);
            const std::string data((std::istreambuf_iterator<char>(file)),{});
            const std::vector<InputImage> images{make_input_image(data)};
            if (check_content(key,"Describe the main color in one word.",images)!=ModerationResult::allowed) return 3;
            const auto reply=process_openai_response({},"Describe the main color in one word.",key,images);
            if (reply.find("red")==std::string::npos && reply.find("Red")==std::string::npos &&
                reply.find("RED")==std::string::npos && reply.find("红")==std::string::npos)
                throw std::runtime_error("Vision did not identify the fixture color");
            std::cout<<"PASS vision: correctly identified fixture color; received "<<reply.size()<<" text bytes\n";
        } else if (mode=="generate" || mode=="edit" || mode=="route_edit") {
            std::vector<InputImage> images;
            if (mode=="edit" || mode=="route_edit") {
                std::ifstream file("build/local-checks/generated-image.png",std::ios::binary);
                const std::string data((std::istreambuf_iterator<char>(file)),{});
                images.push_back(make_input_image(data));
            }
            const std::string prompt=mode=="generate" ? "A simple solid blue circle centered on a plain white background, no text." :
                "Change the blue circle to red. Keep the white background and composition.";
            if (check_content(key,prompt,images)!=ModerationResult::allowed) return 3;
            if (mode=="route_edit") {
                const auto route=infer_image_command(key,prompt,true);
                if (!route.requested || !route.edit_only || route.prompt!=prompt)
                    throw std::runtime_error("Natural edit did not route to image API");
            }
            const auto png=generate_image(key,prompt,images);
            const std::string path=mode=="generate" ? "build/local-checks/generated-image.png" : "build/local-checks/edited-image.png";
            std::ofstream file(path,std::ios::binary);
            file.write(png.data(),static_cast<std::streamsize>(png.size()));
            if (!file) throw std::runtime_error("Could not save integration image");
            std::cout<<"PASS "<<mode<<": saved "<<png.size()<<" PNG bytes inside project build/local-checks\n";
        } else return 2;
    } catch (const ChatError& error) {
        std::cerr<<"FAIL: "<<error.user_message()<<'\n';
        result=1;
    } catch (const std::exception&) {
        std::cerr<<"FAIL: unexpected test/configuration error\n";
        result=1;
    }
    curl_global_cleanup();
    return result;
}
