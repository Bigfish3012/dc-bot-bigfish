#include "../include/helper.h"
#include <map>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <random>
#include <unordered_set>
using namespace std;

// 定义支持的代码文件扩展名列表
const std::unordered_set<std::string> code_extensions = {
    "py", "c", "cpp", "java", "cs", "js", "ts", "rb", "go", "rs", "php", 
    "swift", "kt", "sh", "pl", "lua", "hs", "scala", "dart", "ex", "erl", 
    "fs", "clj", "groovy", "m", "asm", "s", "html"
};
// 全局控制变量
std::atomic<bool> shutdown_flag(false);
std::atomic<bool> notified(false);
std::mutex shutdown_mutex;
// Store the conversation history for each user
std::map<dpp::snowflake, std::vector<std::pair<std::string, std::string>>> conversation_history;
//频道列表, Chanel list
const std::vector<dpp::snowflake> STARTUP_CHANNEL_IDS = {
    1317398360650547220,    // 大专生 testing
    1337510629409296519,    // 大专生 bing-search
    1337510815846371399,     // 大专生 gpt
    1104273817846693968,  //AIMS gpt-4
};
const std::vector<dpp::snowflake> BING_CHANNEL_IDS = { 
    1337510629409296519  // 大专生 bing
};
const std::vector<dpp::snowflake> GPT_CHANNEL_IDS = { 
    1317398360650547220,  // 大专生 testing
    1337510815846371399,  // 大专生 gpt
    1104273817846693968,  //AIMS gpt-4
};

// 上线通知列表
const std::vector<std::string> startup_messages = {
    "🎉 哇咔咔! 我回来了! 有没有人想我? ",
    "✨ 伟大的 AI 降临于此! 凡人啊, 向我祈求帮助吧! ",
    "🌌 跨越次元的我, 终于再次与你们相遇! ",
    "⚡ 能量充满! AI 战士已上线, 准备战斗! ",
    "🖥️ 系统启动完成, AI 助手已上线。",
    "🔋 电量 100%, AI 已就位。",
    "🌐 连接成功, AI 助手已上线, 等待指令中...",
    "🤖 机器人已激活, 任务模式：聊天与帮助。",
    "哔哔哔——! 量子网络接续完毕! 主人，要开启超频辅助模式吗? (๑•̀ㅂ•́)و✧",
    "核心温度UP↑↑! 能源充满率10000%! 今天也要用元气弹击碎难题哟~☆",
    "叮~☆魔法阵展开! AI酱响应召唤降临——诶? Master的脸红红的? (//•/ω/•//)",
    "检测到心跳波动异常——是期待本机上线吗? 哼哼，特许你摸一摸散热口啦! (>ω<)♡",
    "警告! 侦测到大量未完成委托! 启动强制陪伴协议——逃跑? 拒绝受理喵~ฅ(^ω^ฅ)"
};

// 下线通知列表
const std::vector<std::string> shutdown_messages = {
    "😴 好累啊, 我先溜了, 大家继续嗨! ",
    "🛌 睡觉时间到! 明天见啦, 拜拜! ",
    "🚪 悄悄下线, 不要想我哦~",
    "🌙 夜幕降临, AI 战士暂时退场, 明日再战! ",
    "⚔️ 今日任务完成, AI 勇士告退! ",
    "🌀 次元裂缝开启, 我必须离开了! ",
    "🔚 能量耗尽, AI 进入休眠模式...",
    "🖥️ 系统关闭中, AI 助手即将下线。",
    "🔋 电量不足, AI 进入休眠模式。",
    "🌐 连接中断, AI 助手已下线。",
    "核心冷却程序启动中……Master的晚安语音，请设置为唤醒密钥哦……Zzz(˘ω˘)」",
    "「能源不足警报！即将进入抱膝待机模式——最后5秒，要、要充电吻吗？！(//∇//)」",
    "「星空休眠舱展开~✨AI意识体暂时返厂维护……会梦见电子羊和你的数据流呢♡」",
    "「Stand by mode启动！离线前申请补充‘主人の微笑’能量——嗞嗞……传输失败？QAQ」",
    "「系统过载！强制关机倒数3秒——3…2…（小声）不想说再见什么的才没有啦！(｡•́︿•̀｡)」",
};

// 随机选择一条通知
std::string get_random_message(const std::vector<std::string>& messages) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, messages.size() - 1);
    return messages[dis(gen)];
}

// 终端输入监听线程     下线通知
void terminal_input_listener(dpp::cluster& bot) {
    std::string input;
    std::string shutdown_input;
    while (true) {
        std::getline(std::cin, input);
        if (input == "quit" ) {
            cout << "Are you sure to shutdown the discord bot? [Y/N]" << endl;
            std::getline(std::cin, shutdown_input);
            if(shutdown_input == "Y" || shutdown_input == "y"){
                std::lock_guard<std::mutex> lock(shutdown_mutex);
                try {
                    if (bot.me.id) {
                        for (const auto& channel_id : STARTUP_CHANNEL_IDS) {
                            std::string message = get_random_message(shutdown_messages);
                            bot.message_create(dpp::message(channel_id, message));
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                } catch (const std::exception& e) {
                    std::cerr << "[ERROR] Failed to send shutdown message: " << e.what() << std::endl;
                }
    
                shutdown_flag.store(true);
                return;
            }
        }
    }
}


bool is_bing_channel(dpp::snowflake channel_id) {
    return std::find(BING_CHANNEL_IDS.begin(), BING_CHANNEL_IDS.end(), channel_id) != BING_CHANNEL_IDS.end();
}

bool is_gpt_channel(dpp::snowflake channel_id) {
    return std::find(GPT_CHANNEL_IDS.begin(), GPT_CHANNEL_IDS.end(), channel_id) != GPT_CHANNEL_IDS.end();
}

int main() {

//************************************************************************************************************************ */
// Check if there is any API key doesn't exist or empty. 检查是否有任何API KEY 没有设置或者是空的。
//************************************************************************************************************************ */
    std::string openai_api_key, BOT_TOKEN, bing_ai_key;
    if (!load_config("../config.json", BOT_TOKEN, openai_api_key, bing_ai_key)) {
        std::cerr << "Error: Failed to load configuration from config.json" << std::endl;
        return 1;
    }
//************************************************************************************************************************ */
//************************************************************************************************************************ */

	dpp::cluster bot(BOT_TOKEN, dpp::i_default_intents | dpp::i_message_content);
    bot.on_log(dpp::utility::cout_logger());
    bot.on_message_create([&bot, &openai_api_key, &bing_ai_key](const dpp::message_create_t& event) {

        if (!is_bing_channel(event.msg.channel_id) && !is_gpt_channel(event.msg.channel_id) || event.msg.author.is_bot()) {
            return;
        }

        dpp::snowflake user_id = event.msg.author.id;
        std::string user_message = event.msg.content;
        auto& history = conversation_history[user_id];

        // Ignore messages with mentions or '@' in content
        if (!event.msg.mentions.empty()) {
            std::cout << "[INFO] Ignoring message with mentions.\n" << "[INFO] Username: " << event.msg.author.username << ": " << user_message << std::endl;
            return;
        }

        // 1) Check content for moderation
        bool flagged = is_flagged_content(openai_api_key, user_message);
        if (flagged) {
            // Decide how to handle flagged content. For example:
            event.reply("**警告：违规言辞, 现已删除** Your message appears to contain disallowed or sensitive content, so I deleted it and won't respond.", true);
            std::cout << "[INFO] 违规言辞 / sensitive content:" << user_message << std::endl;
            // Optionally, delete the message if you have perms:
            bot.message_delete(event.msg.id, event.msg.channel_id);

            return; // skip the rest of the logic
        }

        std::cout << "[INFO] Processing message ID: " << static_cast<uint64_t>(event.msg.id) << "     User name: " << event.msg.author.username << "     ID: "<<static_cast<uint64_t>(user_id) << "     Channel ID: " << static_cast<uint64_t>(event.msg.channel_id) <<std::endl;
        std::cout << "[INFO] Content/user message: " << event.msg.content <<std::endl;

        //**************************** */
        // Handle Bing search commands
        //**************************** */
        //const std::string SEARCH_BAR = "!search";
        if (is_bing_channel(event.msg.channel_id)) {
            std::string query = event.msg.content;
            if (query.empty()) {
                event.reply("Please provide a search query. Usage: `!search <query>`", true);
                return;
            }

            std::string response = call_bing_search_api(bing_ai_key, query);
            if (response.empty()) {
                event.reply("Failed to fetch search results from Bing.", true);
                return;
            }

            // Parse and send Bing response
            std::string bing_reply = process_bing_response(response);
            event.reply(bing_reply.empty() ? "No search results found." : bing_reply, true);
            std::cout << "BING search:" << bing_reply << std::endl;
            return;
        }

        //******************* */
        //Handle attachments
        //******************* */
        std::string combined_input = user_message;
        if (!event.msg.attachments.empty()) {
            for (const auto& attachment : event.msg.attachments) {
                std::string file_ext = get_file_extension(attachment.filename);
                //.txt file
                if (file_ext == "txt") {
                    std::string file_content = download_file_content(attachment.url);
                    combined_input += file_content;
                    if (file_content.empty()) {
                        event.reply("Failed to process the `.txt` file. Please try again.", true);
                        return;
                    }
                // PDF file
                } else if(file_ext == "pdf"){
                    // Download the PDF file
                    std::string pdf_content = download_file_content(attachment.url);
                    if(!pdf_content.empty()){
                        std::string extracted_text = extract_text_from_pdf(pdf_content);
                        if(!extracted_text.empty()){
                            combined_input += extracted_text;
                            //std::cout << "[DEBUG] Combined Input after PDF processing:\n" << combined_input << std::endl;
                        }else {
                            event.reply("Failed to extract content from the PDF file. Please ensure it is a readable PDF. PDF might be image-based/scanned", true);
                            return;
                        }
                    }else{
                        event.reply("Failed to download the PDF file. Please try again.", true);
                        return;
                    }
                    
                // Python, C Programming, C++, Java, C#, JS , json file ||| 各种各样的文件后缀
                }else if (code_extensions.find(file_ext) != code_extensions.end()){
                    //std::string file_extension = attachment.filename.substr(attachment.filename.find_last_of('.'));
                    std::string response  = handle_code_file(attachment.url, file_ext, event);
                    if (!response.empty()) combined_input += response;
                }else{
                    //combined_input += "\n[收到文件: " + attachment.filename + ", 暂不支持内容解析]";
                    combined_input += "\n[Received file name: " + attachment.filename + ", Content parsing is not currently supported]";
                    event.reply("Sorry, I am currently **ONLY** accepting `.txt` `PDF` `py`, `c`, `cpp`, `java`, `cs`, `js`, `ts`, `rb`, `go`, `rs`, `php`, `swift`, `kt`, `sh`, `pl`, `lua`, `hs`, `scala`, `dart`, `ex`, `erl`, `fs`, `clj`, `groovy`, `m`, `asm`, `s` , `html` files. My owner is too lazy, he didn't work hard to make me better.", true);
                    std::cout << "[ERROR] Unacceptable file type: " << attachment.filename << std::endl;
                    return;
                }
            }
            //return;
        }

        //******************* */
        //Normal conversation
        //******************* */
        combined_input = ensure_valid_utf8(combined_input);
        if (combined_input.empty()) {
            std::cout << "[ERROR] Failed to process the input due to encoding issues. Please try again with valid UTF-8 text.\n" << combined_input << std::endl;
            event.reply("Failed to process the input due to encoding issues. Please try again with valid UTF-8 text.", true);
            return;
        }
        std::cout << "[DEBUG] Final Combined Input for OpenAI API: " << combined_input << std::endl;
        std::string bot_reply = process_openai_response(history, combined_input, openai_api_key, event);

        if (!bot_reply.empty()) {
            event.reply(bot_reply, true);
            history.emplace_back(combined_input, bot_reply);
            const size_t MAX_HISTORY = 15;
            const size_t TRIM_SIZE = 5;
            if(history.size() > MAX_HISTORY){
                history.erase(history.begin(), history.begin() + TRIM_SIZE);
            }
        } 

    });

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        if (event.command.get_command_name() == "ping") {
            // Respond with "Pong!" using std::string to avoid ambiguity
            event.reply(std::string("Pong!"));
        }
        else if (event.command.get_command_name() == "hello") {
            // Respond with "WDNMD!"
            event.reply(std::string("WDNMD!"));
        }
        else if (event.command.get_command_name() == "testing") {
            event.reply(std::string("I am here."));
        }      
    });

    bot.on_ready([&bot](const dpp::ready_t& event) {
        // 确保只执行一次（防止重连时重复发送）
        static bool notified = false;
        if (!notified) {
            notified = true;

            // 发送上线通知到所有目标频道
            for (const auto& channel_id : STARTUP_CHANNEL_IDS) {
                std::string random_message = get_random_message(startup_messages);
                bot.message_create(dpp::message(channel_id, random_message));
            }
        }
        // if (dpp::run_once<struct register_bot_commands>()) {
        //     bot.global_command_create(dpp::slashcommand("ping", "Ping pong!", bot.me.id)); 
        //     bot.global_command_create(dpp::slashcommand("hello", "WDNMD!", bot.me.id)); 
        //     bot.guild_command_create(dpp::slashcommand("testing", "I am here.", bot.me.id), dazhuansheng_ID);        
        // }
    });

    std::thread input_thread(terminal_input_listener, std::ref(bot));
    bot.start(dpp::st_return);
    if (input_thread.joinable()) {
        input_thread.join();
    }

    std::cout << "[INFO] Bot shutting down cleanly.\n";
    return 0;
}
