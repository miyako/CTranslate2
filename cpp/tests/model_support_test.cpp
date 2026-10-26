#include "model_support.h"

#include <chrono>
#include <iostream>

using namespace ct2_server;

static void check(bool condition, const char* description) {
    if (!condition)
        throw std::runtime_error(description);
}

template <typename Function>
static void rejects(Function function) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Expected an explicit error");
}

int main() {
    try {
        const Json user = {{"role", "user"}, {"content", " Hello "}};
        Json request = {{"messages", Json::array({user})}};
        check(native_chat_prompt(request, "qwen3")
              == "<|im_start|>user\nHello<|im_end|>\n<|im_start|>assistant\n", "Qwen3 prompt");
        request["enable_thinking"] = false;
        check(native_chat_prompt(request, "qwen3").find("<think>\n\n</think>\n\n") != std::string::npos,
              "Qwen3 thinking disabled");
        request.erase("enable_thinking");
        check(native_chat_prompt(request, "gemma3")
              == "<bos><start_of_turn>user\nHello<end_of_turn>\n<start_of_turn>model\n", "Gemma3 prompt");
        check(native_chat_prompt(request, "gemma4")
              == "<bos><|turn>user\nHello<turn|>\n<|turn>model\n", "Gemma4 prompt");
        check(chat_format("gemma4", {{"eos_token", "<end_of_turn>"}}) == "gemma3", "Legacy Gemma4 format");
        check(chat_format("gemma4_unified", {{"eos_token", "<turn|>"}}) == "gemma4_unified", "Unified Gemma4 format");
        request["enable_thinking"] = true;
        check(native_chat_prompt(request, "gemma4").find("<bos><|turn>system\n<|think|>\n<turn|>\n") == 0,
              "Gemma4 thinking system turn");
        const Json system = {{"role", "system"}, {"content", "Be brief."}};
        request["messages"] = Json::array({system, user});
        check(native_chat_prompt(request, "gemma3").find("user\nBe brief.\n\nHello") != std::string::npos,
              "Gemma3 merged system turn");
        check(native_chat_prompt(request, "gemma4").find("system\n<|think|>\nBe brief.") != std::string::npos,
              "Gemma4 system and thinking");
        request.erase("enable_thinking");
        const Json assistant = {{"role", "assistant"}, {"content", "Hi."}};
        request["messages"] = Json::array({user, assistant, user});
        check(native_chat_prompt(request, "gemma4").find("<|turn>model\nHi.<turn|>") != std::string::npos,
              "Multi-turn Gemma4");
        request["messages"] = Json::array({user, user});
        rejects([&] { native_chat_prompt(request, "gemma3"); });
        request["messages"] = Json::array({assistant});
        rejects([&] { native_chat_prompt(request, "gemma4"); });
        request["messages"] = Json::array({system, assistant, user});
        rejects([&] { native_chat_prompt(request, "gemma4"); });
        request["messages"] = Json::array({user, system});
        rejects([&] { native_chat_prompt(request, "qwen3"); });
        request["messages"] = Json::array({user});
        request["tools"] = Json::array({{{"type", "function"}}});
        rejects([&] { native_chat_prompt(request, "qwen3"); });
        request.erase("tools");
        request["messages"][0]["content"] = Json::array({{{"type", "image_url"}, {"image_url", "test"}}});
        rejects([&] { native_chat_prompt(request, "gemma3"); });
        request["messages"][0]["content"] = Json::array({{{"type", "text"}, {"text", "Hello"}},
                                                        {{"type", "text"}, {"text", " world"}}});
        check(native_chat_prompt(request, "qwen3").find("Hello world") != std::string::npos, "Text content parts");
        rejects([&] { native_chat_prompt(request, "umt5"); });
        rejects([] { validate_messages(Json::object()); });
        rejects([] { validate_messages({{"messages", Json::array()}}); });

        for (const auto* type : {"t5", "mt5", "umt5", "t5gemma", "t5gemma2"})
            check(is_seq2seq(type) && !is_decoder(type), "Seq2seq capabilities");
        for (const auto* type : {"qwen3", "gemma3_text", "gemma4_text", "gemma4_unified"})
            check(is_decoder(type) && !is_seq2seq(type), "Decoder capabilities");
        check(!is_decoder("roberta") && !is_seq2seq("roberta"), "RoBERTa encoder capabilities");
        check(encoder_length({{"model_type", "roberta"}, {"max_position_embeddings", 514}, {"pad_token_id", 1}}) == 512,
              "RoBERTa positional offset");
        check(encoder_length({{"model_type", "bert"}, {"max_position_embeddings", 512}}) == 512, "BERT positions");
        rejects([] { encoder_length({{"max_position_embeddings", 1}}); });
        check(encoder_input({10, 11, 12}, 0, 2, 4) == std::vector<int32_t>({0, 10, 11, 2}), "Truncate and wrap encoder");
        check(encoder_input({}, 0, 2, 512) == std::vector<int32_t>({0, 2}), "Empty encoder text");
        rejects([] { encoder_input({10}, -1, 2, 512); });

        const Json runtime = {{"bos_token", "<bos>"}, {"eos_token", "</s>"}};
        check(runtime_token(runtime, "bos_token", "fallback") == "<bos>", "Template BOS from model");
        check(runtime_token({{"bos_token", nullptr}}, "bos_token", "fallback") == "fallback", "Nullable BOS");
        const std::vector<std::string> tokens = {"Hello"};
        check(seq2seq_input(tokens, {}, runtime, "umt5") == std::vector<std::string>({"Hello", "</s>"}),
              "UMT5 appends EOS");
        check(seq2seq_input({"Hello", "</s>"}, {}, runtime, "t5").size() == 2, "No duplicate EOS");
        check(seq2seq_input(tokens, {}, runtime, "t5gemma2") == std::vector<std::string>({"<bos>", "Hello"}),
              "T5Gemma2 BOS");
        const auto tokenizer = Json::parse(R"({
            "post_processor": {
                "type": "TemplateProcessing",
                "single": [{"SpecialToken":{"id":"<bos>","type_id":0}},{"Sequence":{"id":"A","type_id":0}},
                           {"SpecialToken":{"id":"</s>","type_id":0}}],
                "special_tokens": {"<bos>":{"tokens":["<bos>"]},"</s>":{"tokens":["</s>"]}}
            }})");
        check(seq2seq_input(tokens, tokenizer, runtime, "t5gemma2")
              == std::vector<std::string>({"<bos>", "Hello", "</s>"}), "Exact tokenizer postprocessor");
        rejects([&] {
            seq2seq_input(tokens, {{"post_processor", {{"type", "UnknownProcessor"}}}}, runtime, "umt5");
        });
        const std::vector<std::string> stops = {"<turn|>", "<end_of_turn>"};
        check(visible_text("Hello<tu", stops, false) == "Hello", "Buffer partial stop token");
        check(visible_text("Hello<turn|>", stops, false) == "Hello", "Strip Gemma4 stop token");
        check(visible_text("Hello<end_of_turn>junk", stops, true) == "Hello", "Strip Gemma3 stop token");
        check(visible_text("Hello<tu", stops, true) == "Hello<tu", "Flush non-stop text at end");

        const auto directory = std::filesystem::temp_directory_path()
            / ("ct2-model-support-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        try {
            std::ofstream(directory / "config.json") << R"({"model_type":"bert"})";
            check(model_type(load_source_config(directory)) == "bert", "Legacy config fallback");
            std::ofstream(directory / "source_config.json") << R"({"model_type":"roberta"})";
            check(model_type(load_source_config(directory)) == "roberta", "HF metadata takes precedence");
            std::ofstream(directory / "source_config.json") << "{broken";
            rejects([&] { load_source_config(directory); });
        } catch (...) {
            std::filesystem::remove_all(directory);
            throw;
        }
        std::filesystem::remove_all(directory);
        std::cout << "Model support tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
