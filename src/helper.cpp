#include "../include/helper.h"

using namespace std;
std::string openai_url = "https://api.openai.com/v1/chat/completions";
// Define size limits of reply message
const size_t DISCORD_MESSAGE_LIMIT = 2000; // 2,000 characters
const size_t MAX_RESPONSE_SIZE = 10 * 1024 * 1024; // 10 MB in bytes


bool ends_with(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

//check api keys and bot token
bool load_config(const std::string& filename, std::string& bot_token,std::string& openai_key,std::string& bing_key) {
    try {
        std::ifstream config_file(filename);
        if (!config_file.is_open()) {
            std::cerr << "Error: Could not open config file: " << filename << std::endl;
            return false;
        }

        nlohmann::json config;
        config_file >> config;

        bot_token = config["BOT_TOKEN"].get<std::string>();
        openai_key = config["OPENAI_API_KEY"].get<std::string>();
        bing_key = config["BING_API_KEY"].get<std::string>();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading config: " << e.what() << std::endl;
        return false;
    }
}

std::string download_file_content(const std::string& file_url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL for file download." << std::endl;
        return "";
    }

    std::string file_content;
    curl_easy_setopt(curl, CURLOPT_URL, file_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file_content);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Failed to download file: " << curl_easy_strerror(res) << std::endl;
        file_content = "";
    }

    if (res == CURLE_OK) {
        curl_easy_cleanup(curl); // Cleanup
        return file_content;
    } else {
        std::cerr << "[ERROR] File download failed with error: " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return ""; // Return an empty string on error
    }

}

std::string call_bing_search_api(const std::string& subscription_key, const std::string& query) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL for Bing API." << endl;
        return "";
    }

    std::string response_string;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Ocp-Apim-Subscription-Key: " + subscription_key).c_str());

    std::string encoded_query = curl_easy_escape(curl, query.c_str(), (int)query.length());
    std::string bing_url = "https://api.bing.microsoft.com/v7.0/search?q=" + encoded_query;
    curl_easy_setopt(curl, CURLOPT_URL, bing_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        cerr << "Bing API call failed: " << curl_easy_strerror(res) << endl;
        response_string = "";
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return response_string;
}

std::string process_bing_response(const std::string& response) {
    try {
        nlohmann::json response_json = nlohmann::json::parse(response);
        if (response_json.contains("webPages")) {
            std::string reply_msg;
            int count = 0;
            for (const auto& item : response_json["webPages"]["value"]) {
                if (count++ == 3) break;
                reply_msg += "**" + item["name"].get<std::string>() + "**\n";
                reply_msg += item["snippet"].get<std::string>() + "\n";
                reply_msg += item["url"].get<std::string>() + "\n\n";
            }
            return reply_msg.empty() ? "No search results found." : reply_msg;
        }
    } catch (const std::exception& e) {
        return "Error parsing Bing API response: " + std::string(e.what());
    }
    return "No search results found.";
}

std::string call_gpt4_api(const std::string& api_key, const nlohmann::json& request_json) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL for OpenAI API." << std::endl;
        return "";
    }

    std::string response_string;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());

    // Debug: Log the JSON payload before sending
    std::string request_body = request_json.dump();

    // DO NOT DELETE the line, the below line is used read the history of the requests.
    //std::cout << "Request Body/ Reply history: " << request_body << std::endl;

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "[ERROR] OpenAI API call failed: " << curl_easy_strerror(res) << std::endl;
        response_string = "";
    } else {
        std::cout << "[DEBUG] OpenAI API call succeeded. Response received." << std::endl;
    }
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return response_string;
}

std::string process_openai_response(
    const std::vector<std::pair<std::string, std::string>>& history,
    const std::string& user_message,
    const std::string& openai_api_key,
    const dpp::message_create_t& event) {

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
        {"temperature", 1.0}
    };

    // Call the OpenAI API
    std::string api_response = call_gpt4_api(openai_api_key, request_json);

    if (api_response.empty()) {
        return "Failed to get a response from OpenAI API. / Open API response is empty `in helper.cpp line 157`";
    }

    try {
        nlohmann::json response_json = nlohmann::json::parse(api_response);
        if (response_json.contains("choices") && !response_json["choices"].empty()) {
            std::string bot_reply = response_json["choices"][0]["message"]["content"].get<std::string>();
            // Calculate the size of the reply in bytes
            size_t reply_size = bot_reply.size();

            if (reply_size > MAX_RESPONSE_SIZE) {
                // Response is too large to send
                return "My response is larger than 10MB, which is the file limit in Discord. Therefore, I can't respond to your question.";
            }
            else if (reply_size > DISCORD_MESSAGE_LIMIT) {
                // Response is large but within the 10 MB limit; send as a file
                std::string file_name = "response_" + event.msg.author.username + ".txt";

                // Save the response to a file
                std::ofstream file(file_name);
                if (file.is_open()) {
                    file << bot_reply;
                    file.close();

                    // Send the file as an attachment
                    dpp::message msg(event.msg.channel_id, "The response is too long. Here is the reply in a file:");
                    msg.add_file(file_name, dpp::utility::read_file(file_name));
                    msg.set_reference(event.msg.id);
                    event.reply(msg, true);
                    cout << "[BOT REPLY]: " << file_name << endl;
                    // Cleanup the file
                    if (std::ifstream(file_name).good()) {
                        std::remove(file_name.c_str());
                    } else {
                        std::cerr << "[ERROR] File not found for cleanup: " << file_name << std::endl;
                    }
                    return ""; // Indicate that the response has been handled
                } else {
                    return "Failed to save the response to a file.";
                }
            }
            else {
                // Log the bot's reply for debugging
                std::cout << "[BOT REPLY] " << bot_reply << std::endl;
                // Response is within Discord's message limit; send directly
                return bot_reply;
            }
        }
        return "Error! Please print the error message to see what is going on\n use 'cout << response_json << endl;' ";
    } catch (const std::exception& e) {
        return "Error processing GPT response: " + std::string(e.what());
    }
}

std::string call_openai_moderation_api(const std::string& api_key, const std::string& content) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[ERROR] Failed to initialize CURL for OpenAI Moderation." << std::endl;
        return "";
    }

    // The Moderation endpoint
    std::string moderation_url = "https://api.openai.com/v1/moderations";

    // Create the JSON body
    nlohmann::json request_json = {
        {"input", content},
        {"model", "text-moderation-latest"}
        
    };
    std::string request_body = request_json.dump();

    // Prepare to collect response
    std::string response_string;
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());

    // Set up cURL
    curl_easy_setopt(curl, CURLOPT_URL, moderation_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "[ERROR] OpenAI Moderation API call failed: " << curl_easy_strerror(res) << std::endl;
        response_string.clear();
    } else {
        std::cout << "[DEBUG] OpenAI Moderation call succeeded. Response received." << std::endl;
    }

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return response_string;
}

std::string extract_text_from_pdf(const std::string& pdf_content) {
    std::cerr << "[DEBUG] Starting PDF text extraction..." << std::endl;
    std::string temp_pdf_path = "/tmp/temp_file.pdf";
    std::ofstream pdf_file(temp_pdf_path, std::ios::binary);
    if (!pdf_file.is_open()) {
        std::cerr << "[ERROR] Failed to open temporary file for writing." << std::endl;
        return "[ERROR] PDF_EXTRACT_OPEN_FAILED";
    }
    pdf_file.write(pdf_content.c_str(), pdf_content.size());
    pdf_file.close();

    std::string command = "pdftotext \"" + temp_pdf_path + "\" -";
    std::cerr << "[DEBUG] Executing command: " << command << std::endl;
    std::string text_content;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "[ERROR] Failed to execute pdftotext command." << std::endl;
        return "[ERROR] PDF_EXTRACT_COMMAND_FAILED";
    }

    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        text_content += buffer;
    }
    int return_code = pclose(pipe);
    if (return_code != 0) {
        std::cerr << "[ERROR] pdftotext failed with return code: " << return_code << std::endl;
        text_content.clear(); // Clear out partial or corrupt data
    }

    remove(temp_pdf_path.c_str());
    //std::cerr << "[DEBUG] Extracted text: " << text_content << std::endl;
    return text_content;
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

std::string handle_code_file(const std::string& file_url, const std::string& file_extension, const dpp::message_create_t& event) {
    std::cout << "[INFO] Downloading code file from URL: " << file_url << std::endl;

    // Download the file content
    std::string file_content = download_file_content(file_url);
    if (file_content.empty()) {
        std::cerr << "[ERROR] Failed to download code file content." << std::endl;
        return "Failed to download the `" + file_extension + "` file. Please try again.";
    }

    std::cout << "[INFO] Code file content downloaded successfully." << std::endl;

    // Check if the content exceeds Discord's message limit
    //const size_t DISCORD_MESSAGE_LIMIT = 2000; // 2,000 characters
    if (file_content.size() > DISCORD_MESSAGE_LIMIT) {
        std::string file_name = "processed_code_file" + file_extension;

        // Save content to a file
        std::ofstream out_file(file_name);
        if (out_file.is_open()) {
            out_file << file_content;
            out_file.close();

            // Create a message with the file attached
            dpp::message reply(event.msg.channel_id, "The `" + file_extension + "` file is too large to send as a message. Here's the content as a file:");
            reply.add_file(file_name, dpp::utility::read_file(file_name));
            reply.set_reference(event.msg.id);

            // Clean up the temporary file
            if (std::remove(file_name.c_str()) == 0) {
                std::cout << "[INFO] Temporary file cleaned up successfully." << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to clean up the temporary file." << std::endl;
            }

            return "\nHere is the content of the `" + file_extension + "` file:\n" + file_content; // if the response is greater than DISCORD_MESSAGE_LIMIT, then reply as a file
        } else {
            std::cerr << "[ERROR] Failed to save code file content to a temporary file." << std::endl;
            return "Failed to save the `" + file_extension + "` file content to a file.";
        }
    } else {
        // Return the content for smaller files
        return "\nHere is the content of the `" + file_extension + "` file:\n" + file_content;
    }
}

bool is_flagged_content(const std::string& api_key, const std::string& content) {
    std::string api_response = call_openai_moderation_api(api_key, content);

    if (api_response.empty()) {
        // Treat empty response as "error" or "not flagged"; your choice
        std::cerr << "[WARN] Empty Moderation API response." << std::endl;
        return false;
    }

    try {
        nlohmann::json response_json = nlohmann::json::parse(api_response);
        if (response_json.contains("results") && !response_json["results"].empty()) {
            bool flagged = response_json["results"][0]["flagged"].get<bool>();
            return flagged;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to parse Moderation response: " << e.what() << std::endl;
    }
    return false;
}

std::string get_file_extension(const std::string& filename) {
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return ""; // 无扩展名
    }
    return filename.substr(dot_pos + 1);
}