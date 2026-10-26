#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ct2_server {

using Json = nlohmann::json;

inline Json read_config(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Could not open configuration: " + path.string());
    try {
        auto config = Json::parse(input);
        if (!config.is_object())
            throw std::runtime_error("Configuration must be an object: " + path.string());
        return config;
    } catch (const Json::exception& error) {
        throw std::runtime_error("Invalid configuration " + path.string() + ": " + error.what());
    }
}

inline Json load_source_config(const std::filesystem::path& directory) {
    // Keep HF metadata separate: config.json belongs to the CT2 runtime.
    for (const auto* name : {"source_config.json", "config.json"}) {
        const auto path = directory / name;
        if (std::filesystem::exists(path))
            return read_config(path);
    }
    return Json::object();
}

inline std::string model_type(const Json& config) {
    return config.value("model_type", std::string());
}

inline std::string runtime_token(const Json& config, const std::string& name, const std::string& fallback) {
    return config.contains(name) && config[name].is_string() ? config[name].get<std::string>() : fallback;
}

inline bool is_seq2seq(const std::string& type) {
    return type == "t5" || type == "mt5" || type == "umt5"
        || type == "t5gemma" || type == "t5gemma2";
}

inline bool is_gemma3(const std::string& type) {
    return type == "gemma3" || type == "gemma3_text";
}

inline bool is_gemma4(const std::string& type) {
    return type == "gemma4" || type == "gemma4_text"
        || type == "gemma4_unified" || type == "gemma4_unified_text";
}

inline bool has_native_chat(const std::string& type) {
    return type == "qwen3" || is_gemma3(type) || is_gemma4(type);
}

inline std::string chat_format(const std::string& type, const Json& runtime_config) {
    if (is_gemma4(type) && runtime_config.value("eos_token", "") == "<end_of_turn>")
        return "gemma3";
    return type;
}

inline bool is_decoder(const std::string& type) {
    return has_native_chat(type) || type == "qwen2" || type == "llama"
        || type == "mistral" || type == "gemma" || type == "gemma2"
        || type == "phi3";
}

inline const Json& text_config(const Json& config) {
    if (config.contains("text_config") && config["text_config"].is_object())
        return config["text_config"];
    return config;
}

inline int encoder_length(const Json& config) {
    const auto& text = text_config(config);
    int length = text.value("max_position_embeddings", 512);
    const auto type = model_type(config);
    if (type == "roberta" || type == "xlm-roberta" || type == "camembert")
        length -= text.value("pad_token_id", 1) + 1;
    if (length < 2)
        throw std::runtime_error("Encoder context length must allow at least two special tokens");
    return length;
}

inline std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return "";
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

inline std::string text_content(const Json& message) {
    if (!message.contains("content"))
        throw std::invalid_argument("Every message must contain text content");
    const auto& content = message["content"];
    if (content.is_string())
        return content.get<std::string>();
    if (content.is_array()) {
        std::string result;
        for (const auto& part : content) {
            if (!part.is_object() || part.value("type", "") != "text"
                || !part.contains("text") || !part["text"].is_string())
                throw std::invalid_argument("Only text messages are supported; image/audio/video inference is unavailable");
            result += part["text"].get<std::string>();
        }
        return result;
    }
    throw std::invalid_argument("Message content must be a string or text parts");
}

inline void validate_messages(const Json& data) {
    if (!data.contains("messages") || !data["messages"].is_array() || data["messages"].empty())
        throw std::invalid_argument("'messages' must be a non-empty array");
    for (const auto& message : data["messages"]) {
        if (!message.is_object() || !message.contains("role") || !message["role"].is_string())
            throw std::invalid_argument("Every message must have a role");
        if (!(message.contains("tool_calls")
              && (!message.contains("content") || message["content"].is_null())))
            text_content(message);
    }
}

inline std::string native_chat_prompt(const Json& data, const std::string& type) {
    validate_messages(data);
    if (!has_native_chat(type))
        throw std::invalid_argument("This model requires an explicit Inja-compatible chat template (-j)");
    if ((data.contains("tools") && !data["tools"].empty()))
        throw std::invalid_argument("Native text chat does not support tools; use a compatible -j template for Qwen tool calls");

    std::string prompt = type == "qwen3" ? "" : "<bos>";
    std::string system;
    std::string previous_role;
    const auto& messages = data["messages"];
    for (size_t i = 0; i < messages.size(); ++i) {
        const auto& message = messages[i];
        auto role = message["role"].get<std::string>();
        const auto content = trim(text_content(message));
        if (message.contains("tool_calls"))
            throw std::invalid_argument("Native text chat does not support tool-call history");
        if (role == "system" || role == "developer") {
            if (i != 0)
                throw std::invalid_argument("System/developer message must be first");
            if (is_gemma3(type)) {
                system = content + "\n\n";
                continue;
            }
            role = "system";
        } else if (role != "user" && role != "assistant") {
            throw std::invalid_argument("Unsupported role in native text chat: " + role);
        }
        if (type == "qwen3") {
            prompt += "<|im_start|>" + role + "\n" + content + "<|im_end|>\n";
        } else {
            if (role != "system" && role == previous_role)
                throw std::invalid_argument("Gemma user and assistant turns must alternate");
            if ((previous_role.empty() || previous_role == "system") && role == "assistant")
                throw std::invalid_argument("Gemma conversation must start with a user turn");
            const auto model_role = role == "assistant" ? "model" : role;
            prompt += is_gemma4(type) ? "<|turn>" : "<start_of_turn>";
            prompt += model_role + "\n";
            if (is_gemma4(type) && role == "system" && data.value("enable_thinking", false))
                prompt += "<|think|>\n";
            prompt += system + content;
            system.clear();
            prompt += is_gemma4(type) ? "<turn|>\n" : "<end_of_turn>\n";
            previous_role = role;
        }
    }
    if (is_gemma4(type) && data.value("enable_thinking", false)
        && messages[0].value("role", "") != "system" && messages[0].value("role", "") != "developer")
        prompt.insert(5, "<|turn>system\n<|think|>\n<turn|>\n");
    if (type == "qwen3") {
        prompt += "<|im_start|>assistant\n";
        if (!data.value("enable_thinking", true))
            prompt += "<think>\n\n</think>\n\n";
    } else {
        if (previous_role != "user")
            throw std::invalid_argument("Gemma text chat must end with a user turn");
        prompt += is_gemma4(type) ? "<|turn>model\n" : "<start_of_turn>model\n";
    }
    return prompt;
}

inline std::vector<int32_t> encoder_input(std::vector<int32_t> ids, int cls, int sep, int limit) {
    if (cls < 0 || sep < 0 || limit < 2)
        throw std::runtime_error("Invalid encoder special tokens or context length");
    if (ids.size() > static_cast<size_t>(limit - 2))
        ids.resize(limit - 2);
    ids.insert(ids.begin(), cls);
    ids.push_back(sep);
    return ids;
}

inline std::string visible_text(const std::string& text, const std::vector<std::string>& stops, bool final) {
    size_t end = text.size();
    for (const auto& stop : stops) {
        if (stop.empty())
            continue;
        const auto position = text.find(stop);
        if (position != std::string::npos)
            end = std::min(end, position);
        if (!final) {
            for (size_t length = 1; length < stop.size() && length <= text.size(); ++length) {
                if (text.compare(text.size() - length, length, stop, 0, length) == 0)
                    end = std::min(end, text.size() - length);
            }
        }
    }
    return text.substr(0, end);
}

inline std::vector<std::string> seq2seq_input(const std::vector<std::string>& tokens,
                                             const Json& tokenizer_config,
                                             const Json& runtime_config,
                                             const std::string& type) {
    if (tokenizer_config.contains("post_processor") && tokenizer_config["post_processor"].is_object()) {
        const auto& processor = tokenizer_config["post_processor"];
        if (processor.value("type", "") == "TemplateProcessing") {
            std::vector<std::string> result;
            for (const auto& item : processor.at("single")) {
                if (item.contains("Sequence")) {
                    if (item["Sequence"].value("id", "") != "A")
                        throw std::runtime_error("Unsupported seq2seq tokenizer sequence template");
                    result.insert(result.end(), tokens.begin(), tokens.end());
                } else if (item.contains("SpecialToken")) {
                    const auto name = item["SpecialToken"].at("id").get<std::string>();
                    for (const auto& token : processor.at("special_tokens").at(name).at("tokens"))
                        result.push_back(token.get<std::string>());
                } else {
                    throw std::runtime_error("Unsupported seq2seq tokenizer template item");
                }
            }
            return result;
        }
        if (processor.value("type", "") != "ByteLevel")
            throw std::runtime_error("Unsupported seq2seq tokenizer postprocessor: " + processor.value("type", ""));
    }
    auto result = tokens;
    if (type == "t5gemma" || type == "t5gemma2") {
        const auto bos = runtime_config.at("bos_token").get<std::string>();
        result.insert(result.begin(), bos);
    } else {
        const auto eos = runtime_config.value("eos_token", std::string("</s>"));
        if (result.empty() || result.back() != eos)
            result.push_back(eos);
    }
    return result;
}

}  // namespace ct2_server
