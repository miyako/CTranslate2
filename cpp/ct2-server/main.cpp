//
//  main.cpp
//  ct2-server
//
//  Created by miyako on 2026/01/09.
//

#include "ct2-server.h"

namespace fs = std::filesystem;
using namespace tokenizers;

static std::string LoadBytesFromFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) throw std::runtime_error("Could not open file: " + path);
    
    ifs.seekg(0, std::ios::end);
    size_t size = ifs.tellg();
    std::string data(size, '\0');
    ifs.seekg(0, std::ios::beg);
    ifs.read(&data[0], size);
    
    return data;
}

static void LoadSpecialTokenIds(const std::string& model_path,
                                RerankingMode ranking_mode,
                                int& cls_id,
                                int& sep_id) {
    
    // 1. Set Defaults based on architecture
    switch (ranking_mode) {
        case RERANKING_MODERNBERT:
            cls_id = 50281;
            sep_id = 50282;
            break;
        case RERANKING_ROBERTA:
            cls_id = 0;
            sep_id = 2;
            break;
        case RERANKING_BERT:
        default:
            cls_id = 101;
            sep_id = 102;
            break;
    }
    
    // 2. Try to read overrides from config.json
    fs::path config_path = fs::path(model_path);
    if (fs::is_directory(config_path)) {
        config_path = config_path / "config.json";
    }
    
    if (fs::exists(config_path) && config_path.extension() == ".json") {
        std::string json = LoadBytesFromFile(config_path.string());
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errors) && root.isObject()) {
            
            // Look for CLS or BOS token
            if (root.isMember("cls_token_id") && root["cls_token_id"].isNumeric()) {
                cls_id = root["cls_token_id"].asInt();
            } else if (root.isMember("bos_token_id") && root["bos_token_id"].isNumeric()) {
                cls_id = root["bos_token_id"].asInt();
            }
            
            // Look for SEP or EOS token
            if (root.isMember("sep_token_id") && root["sep_token_id"].isNumeric()) {
                sep_id = root["sep_token_id"].asInt();
            } else if (root.isMember("eos_token_id") && root["eos_token_id"].isNumeric()) {
                sep_id = root["eos_token_id"].asInt();
            }
        }
    }
    std::cout << "[Tokens] CLS/BOS ID: " << cls_id << " | SEP/EOS ID: " << sep_id << std::endl;
}

static long long get_created_timestamp() {
    // std::time(nullptr) returns the current time as a time_t (seconds since epoch)
    return static_cast<long long>(std::time(nullptr));
}

static std::string get_openai_style_id() {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    
    std::string id = "chatcmpl-";
    std::random_device rd;

    static thread_local std::mt19937 gen(std::random_device{}());

    std::uniform_int_distribution<> dis(0, max_index - 1);
    
    for (int i = 0; i < 29; ++i) {
        id += charset[dis(gen)];
    }
    return id;
}

static int GetOptimalIntraOpThreads() {
    int threads = 0;

    // --- macOS Implementation ---
#if defined(__APPLE__)
    int32_t p_cores = 0, e_cores = 0;
    size_t size = sizeof(int32_t);
    sysctlbyname("hw.perflevel0.physicalcpu", &p_cores, &size, NULL, 0);
    sysctlbyname("hw.perflevel1.physicalcpu", &e_cores, &size, NULL, 0);
    threads = p_cores + e_cores; // all 8 on M1
//    #if defined(__APPLE__)
//        int32_t core_count = 0;
//        size_t size = sizeof(core_count);
//        if (sysctlbyname("hw.perflevel0.physicalcpu", &core_count, &size, NULL, 0) == 0) {
//            threads = core_count;
//        }
//        else if (sysctlbyname("hw.physicalcpu", &core_count, &size, NULL, 0) == 0) {
//            threads = core_count;
//        }
//        else {
//            threads = std::thread::hardware_concurrency();
//        }
#else  // Windows and Linux
    unsigned int logical_cores = std::thread::hardware_concurrency();
    threads = (logical_cores > 4) ? (int)(logical_cores / 2) : (int)logical_cores;
#endif
    return std::max(1, std::min(threads, 16));
}

struct RerankResult {
    int index;          // Original index in the document list
    float score;        // Relevance score
    std::string text;   // (Optional) The document text
};

struct RerankItem {
    std::vector<int> ids;
    std::vector<int> type_ids;
};

struct ParsedToolCall {
    std::string name;
    std::string arguments;
};

struct TranslateInput {
    std::string text;
    std::string lang;
};

// Parses the JSON content extracted from between <tool_call>…</tool_call>.
// The model may emit a single object {"name":…,"arguments":…}
// or an array  [{"name":…,"arguments":…}, …].
// Always returns a (possibly empty) list.
static std::vector<ParsedToolCall> parse_tool_call_json(const std::string& json_str) {
    std::vector<ParsedToolCall> results;
    try {
        nlohmann::json parsed = nlohmann::json::parse(json_str);

        // Normalise to an array so downstream code is uniform
        nlohmann::json calls = parsed.is_array() ? parsed : nlohmann::json::array({parsed});

        for (const auto& call : calls) {
            if (!call.contains("name") || !call.contains("arguments")) continue;
            ParsedToolCall tc;
            tc.name = call["name"].get<std::string>();
            tc.arguments = call["arguments"].is_object()
                               ? call["arguments"].dump()
                               : call["arguments"].get<std::string>();
            results.push_back(std::move(tc));
        }
    } catch (...) {
        // Malformed JSON — return empty, callers treat this as not-a-tool-call
    }
    return results;
}

static // Helper to read the template file from the model directory
int LoadMaxPositionEmbeddings(const std::string& model_path) {
    fs::path path(model_path);
    fs::path config_path = path;

    if (fs::is_directory(path)) {
        config_path = path / "config.json";
    }
    
    if (fs::exists(config_path) && config_path.extension() == ".json") {
//        std::cout << "Loading max_position_embeddings from: " << config_path << std::endl;
        
        std::string json = LoadBytesFromFile(config_path.string());
        
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        bool parse = reader->parse(json.c_str(),
                                   json.c_str() + json.size(),
                                   &root,
                                   &errors);
        
        if(parse)
        {
            if(root.isObject())
            {
                Json::Value max_position_embeddings_node = root["max_position_embeddings"];
                if(max_position_embeddings_node.isNumeric())
                {
                    return  max_position_embeddings_node.asInt();
                }
            }
        }
    }
    
    return 512;
}

static const std::unordered_map<std::string, RerankingMode> kModelTypeMap = {
    {"xlm-roberta", RERANKING_ROBERTA}, {"roberta", RERANKING_ROBERTA}, {"camembert", RERANKING_ROBERTA},
    {"bert", RERANKING_BERT}, {"mpnet", RERANKING_BERT}, {"deberta-v2", RERANKING_BERT}, {"modernbert", RERANKING_MODERNBERT},
    {"qwen3", RERANKING_LLM}, {"qwen2", RERANKING_LLM}, {"mistral", RERANKING_LLM},
    {"llama", RERANKING_LLM}, {"gemma", RERANKING_LLM}, {"gemma2", RERANKING_LLM}, {"phi3", RERANKING_LLM},
};

static
RerankingMode LoadRerankingMode(const std::string& model_path) {
    fs::path path(model_path);
    fs::path config_path = path;
    
    if (fs::is_directory(path)) {
        config_path = path / "config.json";
    }
    
    if (fs::exists(config_path) && config_path.extension() == ".json") {
        
        std::string json = LoadBytesFromFile(config_path.string());
        
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        bool parse = reader->parse(json.c_str(),
                                   json.c_str() + json.size(),
                                   &root,
                                   &errors);
        
        if(parse)
        {
            if(root.isObject())
            {
                Json::Value model_type_node = root["model_type"];
                if(model_type_node.isString())
                {
                    std::string model_type = model_type_node.asString();
                    
                    auto it = kModelTypeMap.find(model_type);
                    if (it != kModelTypeMap.end()) {
                        std::cout << "[Rerank] model_type: " << model_type << std::endl;
                        return it->second;
                    }
                    std::cout << "[Rerank] model_type: '" << model_type << "' unrecognized, defaulting to roberta" << std::endl;
                }
            }
        }
    }
    
    return RERANKING_ROBERTA;
}

static
std::string LoadChatTemplate(const std::string& model_path) {
    fs::path path(model_path);
    fs::path chat_template_path = path;

    if (fs::is_directory(path)) {
        chat_template_path = path / "chat_template.jinja";
    }
    
    if (fs::exists(chat_template_path) && chat_template_path.extension() == ".jinja") {
//        std::cout << "[Chat] Loading jinja from: " << chat_template_path << std::endl;
        return LoadBytesFromFile(chat_template_path.string());
    }
    
    return "";
}

static void parse_request_chat_completion(const std::string &json,
                                          std::string &prompt,
                                          std::string &chat_template,
                                          ctranslate2::GenerationOptions &options,
                                          bool &is_stream,
                                          int &n,
                                          bool &has_tools) {
    nlohmann::json data;
    try {
        data = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse request JSON: " << e.what() << std::endl;
        return;
    }

    // --- 1. Template rendering ---
    try {
        if (!data.contains("bos_token")) data["bos_token"] = "<|im_start|>";
        if (!data.contains("eos_token")) data["eos_token"] = "<|im_end|>";

        has_tools = data.contains("tools") && data["tools"].is_array() && !data["tools"].empty();
        data["has_tools"] = has_tools;

        inja::Environment env;
        env.add_callback("dump", 1, [](inja::Arguments& args) {
            if (args.at(0)->is_object() || args.at(0)->is_array()) {
                return args.at(0)->dump();
            }
            return args.at(0)->get<std::string>();
        });

        prompt = env.render(chat_template, data);

    } catch (const std::exception& e) {
        std::cerr << "Template rendering failed: " << e.what() << std::endl;
    }

    // --- 2. Parse generation options ---
    is_stream = data.value("stream", false);
    n         = data.value("n", 1);

    options.sampling_temperature  = data.value("temperature", 0.7);
    options.sampling_topp         = data.value("top_p", options.sampling_topp);
    options.include_prompt_in_result = false;

    if (data.contains("max_tokens") && data["max_tokens"].is_number())
        options.max_length = data["max_tokens"].get<unsigned int>();
    else if (data.contains("max_completion_tokens") && data["max_completion_tokens"].is_number())
        options.max_length = data["max_completion_tokens"].get<unsigned int>();
    else
        options.max_length = 512;
}

static // Unified Loader
std::unique_ptr<tokenizers::Tokenizer> LoadTokenizer(const std::string& model_path) {
    fs::path path(model_path);
    
    // 1. Check if the path points to a directory or a specific file
    fs::path json_path = path;
    fs::path model_file_path = path;

    if (fs::is_directory(path)) {
        // If user gave a folder, look for standard names
        json_path = path / "tokenizer.json";
        model_file_path = path / "tokenizer.model";
    }

    // 2. Try to load Hugging Face JSON first (preferred for modern models)
    if (fs::exists(json_path) && json_path.extension() == ".json") {
        std::cout << "Loading HF Tokenizer from: " << json_path << std::endl;
        std::string blob = LoadBytesFromFile(json_path.string());
        return tokenizers::Tokenizer::FromBlobJSON(blob);
    }
    
    // 3. Fallback to SentencePiece
    if (fs::exists(model_file_path) && model_file_path.extension() == ".model") {
        std::cout << "Loading SentencePiece from: " << model_file_path << std::endl;
        std::string blob = LoadBytesFromFile(model_file_path.string());
        return tokenizers::Tokenizer::FromBlobSentencePiece(blob);
    }

    return nullptr;
}

class GeneratePipeline {
public:
    GeneratePipeline(const std::string& model_dir,
                     int num_threads = 4,
                     const std::string& device = "cpu") {
        
        if (num_threads > 0) {
            ctranslate2::set_num_threads(num_threads);
        }

        fs::path sp_path = fs::path(model_dir) / "tokenizer.model";
        if(fs::exists(sp_path)) {
            tokenizer_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
            auto status = tokenizer_->Load(sp_path.string());
            if (!status.ok()) {
                throw std::runtime_error("GeneratePipeline: failed to load tokenizer: " + status.ToString());
            }
        }
        
        try {
            ctranslate2::Device device_type = (device == "cuda") ?
                ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
            translator_ = std::make_unique<ctranslate2::Translator>(model_dir, device_type);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load CTranslate2 Translator: " + std::string(e.what()));
        }
    }

    std::string generate_batch(const std::vector<std::string>& prompts,
                               ctranslate2::TranslationOptions options,
                               const std::string& model_name) {
        // Collect results per prompt
        size_t n = prompts.size();
        std::vector<std::string> final_texts(n, "");
        
        size_t prompt_tokens = 0;
        size_t completion_tokens = 0;

        options.beam_size = 1;
        
        // Reuse generate_batch_stream internally
        generate_batch_stream(
            prompts,
            options,
            [&](const std::string& delta, int idx) -> bool {
                if (idx >= 0 && (size_t)idx < n) {
                    final_texts[idx] += delta;
                }
                return true; // always continue
            },
            prompt_tokens,
            completion_tokens
        );

        // Build JSON response
        Json::Value root(Json::objectValue);
        root["model"]  = model_name;
        root["object"] = "generate.completion";

        Json::Value resultsNode(Json::arrayValue);
        for (size_t i = 0; i < n; ++i) {
            Json::Value entry(Json::objectValue);
            entry["index"] = (int)i;
            entry["text"]  = final_texts[i];
            resultsNode.append(entry);
        }
        root["results"] = resultsNode;

        Json::Value usage(Json::objectValue);
        usage["prompt_tokens"]     = (Json::UInt64)prompt_tokens;
        usage["completion_tokens"] = (Json::UInt64)completion_tokens;
        usage["total_tokens"]      = (Json::UInt64)(prompt_tokens + completion_tokens);
        root["usage"] = usage;

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        return Json::writeString(writer, root);
    }

    // Streaming variant: fires on_token(delta, prompt_idx) for each new piece of text.
    // Returns total usage counts via out-params after inference completes.
    void generate_batch_stream(const std::vector<std::string>& prompts,
                               ctranslate2::TranslationOptions options,
                               std::function<bool(const std::string&, int)> on_token,
                               size_t& out_prompt_tokens,
                               size_t& out_completion_tokens) {
        std::vector<std::vector<std::string>> batch_tokens;
        batch_tokens.reserve(prompts.size());
        for (const auto& p : prompts) {
            std::vector<std::string> toks;
            tokenizer_->Encode(p, &toks);
            batch_tokens.push_back(std::move(toks));
        }

        out_prompt_tokens = 0;
        for (const auto& t : batch_tokens) out_prompt_tokens += t.size();
        out_completion_tokens = 0;

        size_t n = prompts.size();
        std::vector<std::vector<size_t>> current_ids(n);
        std::vector<std::string>         previous_text(n, "");
        std::vector<bool>                finished(n, false);

        options.callback = [&](ctranslate2::GenerationStepResult step) -> bool {
            size_t sid = step.batch_id;
            
            // ✅ Skip finished sentences but DON'T stop the batch
            if (sid >= n || finished[sid]) return false;

            current_ids[sid].push_back(step.token_id);

            std::string current_text;
            std::vector<int> id_ints(current_ids[sid].begin(), current_ids[sid].end());
            tokenizer_->Decode(id_ints, &current_text);

            if (current_text.length() > previous_text[sid].length()) {
                // Guard incomplete UTF-8
                if (current_text.find("\xef\xbf\xbd") == std::string::npos) {
                    std::string delta = current_text.substr(previous_text[sid].length());
                    previous_text[sid] = current_text;
                    if (!delta.empty()) {
                        ++out_completion_tokens;
                        if (!on_token(delta, (int)sid)) return true;
                    }
                }
            }

            if (step.is_last) finished[sid] = true;

            // ✅ Only stop when ALL sentences are done
            bool all_done = true;
            for (bool f : finished) if (!f) { all_done = false; break; }
            return all_done;
        };

        auto futures = translator_->translate_batch_async(batch_tokens, options);
        
        try {
            for (auto& f : futures) f.get();
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
    }

private:
    std::unique_ptr<ctranslate2::Translator> translator_;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> tokenizer_;
};

#define CACHE_TOKEN_IDS 0

class ChatPipeline {
public:
    ChatPipeline(const std::string& model_dir,
                       int num_threads = 4,
                       const std::string& device = "cpu") {
        
        if (num_threads > 0) {
            ctranslate2::set_num_threads(num_threads);
        }

        tokenizer_ = LoadTokenizer(model_dir);
        if (!tokenizer_) throw std::runtime_error("No tokenizer found for ChatPipeline");

#if CACHE_TOKEN_IDS
        id_to_token_cache_.resize(tokenizer_->GetVocabSize());
        for (size_t i = 0; i < tokenizer_->GetVocabSize(); ++i) {
            id_to_token_cache_[i] = tokenizer_->IdToToken((int32_t)i);
        }
#endif
        try {
            ctranslate2::Device device_type = (device == "cuda") ?
                ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
            generator_ = std::make_unique<ctranslate2::Generator>(model_dir, device_type);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load CTranslate2 Generator: " + std::string(e.what()));
        }
    }

    void chat_completion_stream(const std::string& prompt,
                                ctranslate2::GenerationOptions options,
                                int n,
                                bool has_tools,
                                std::function<bool(const std::string&, int, bool)> on_token) {
        
        std::vector<int> prompt_ids = tokenizer_->Encode(prompt);
        std::vector<std::string> prompt_tokens;
        prompt_tokens.reserve(prompt_ids.size());
        

#if CACHE_TOKEN_IDS
        for (int id : prompt_ids) prompt_tokens.push_back(id_to_token_cache_[id]);
#else
        for (int id : prompt_ids) prompt_tokens.push_back(tokenizer_->IdToToken(id));
#endif

        // Duplicate the prompt 'n' times to process them as a batch
        std::vector<std::vector<std::string>> batch_tokens(n, prompt_tokens);
        
        // State arrays for 'n' parallel generations
        std::vector<std::vector<int>> current_ids(n);
        std::vector<std::string> previous_text(n, "");
        std::vector<bool> finished(n, false);
        std::vector<bool> tool_mode(n, false);
        
        options.callback = [&](ctranslate2::GenerationStepResult step_result) {
            
            size_t batch_id = step_result.batch_id;
            if (finished[batch_id]) return true;
            
            current_ids[batch_id].push_back((int)step_result.token_id);
            std::string current_text = tokenizer_->Decode(current_ids[batch_id]);
            
            bool hit_stop = false;
            // Only check the suffix that could contain a newly completed stop word
            size_t max_sw_len = 20; // precompute once
            size_t check_from = current_text.size() > max_sw_len
                ? current_text.size() - max_sw_len : 0;
            std::string_view suffix(current_text.c_str() + check_from, current_text.size() - check_from);
            for (const auto& word : stop_words_) {
                if (suffix.find(word) != std::string_view::npos) { hit_stop = true; break; }
            }
            
            // 1. DYNAMIC TOOL INTERCEPTION
            if (has_tools && !tool_mode[batch_id]) {
                size_t tag_pos = current_text.find("<tool_call>");
                if (tag_pos != std::string::npos) {
                    tool_mode[batch_id] = true;
                    std::string text_before_tag = current_text.substr(0, tag_pos);
                    if (text_before_tag.length() > previous_text[batch_id].length()) {
                        std::string new_text = text_before_tag.substr(previous_text[batch_id].length());
                        if (!new_text.empty()) {
                            if (!on_token(new_text, (int)batch_id, false)) return true;
                        }
                    }
                    previous_text[batch_id] = current_text;
                }
            }
            
            // 2. TOOL MODE (Silent JSON Buffering)
            if (tool_mode[batch_id]) {
                if (hit_stop || step_result.is_last || current_text.find("</tool_call>") != std::string::npos) {
                    finished[batch_id] = true;
                    
                    size_t start = current_text.find("<tool_call>");
                    size_t end = current_text.find("</tool_call>");
                    
                    std::string json_str = "";
                    if (start != std::string::npos && end != std::string::npos) {
                        json_str = current_text.substr(start + 11, end - (start + 11));
                    } else if (start != std::string::npos) {
                        json_str = current_text.substr(start + 11);
                        size_t earliest = std::string::npos;
                        for (const auto& word : stop_words_) {
                            size_t pos = json_str.find(word);
                            if (pos != std::string::npos) {
                                if (earliest == std::string::npos || pos < earliest)
                                    earliest = pos;
                            }
                        }
                        if (earliest != std::string::npos)
                            json_str = json_str.substr(0, earliest);
                    }
                    
                    // Fire the tool callback!
                    if (!json_str.empty()) {
                        if (!on_token(json_str, (int)batch_id, true)) return true;
                    }
                    
                    bool all_done = true;
                    for (bool f : finished) if (!f) all_done = false;
                    if (all_done) return true;
                }
                return false;
            }
            
            // 3. NORMAL TEXT STREAMING
            if (current_text.length() > previous_text[batch_id].length()) {
                std::string new_text = current_text.substr(previous_text[batch_id].length());
                
                if (hit_stop) {
                    finished[batch_id] = true;
                    
                    // Strip the stop word before sending to the client
                    for (const auto& word : stop_words_) {
                        size_t pos = new_text.find(word);
                        if (pos != std::string::npos) new_text = new_text.substr(0, pos);
                    }
                    
                    if (!new_text.empty()) {
                        if (!on_token(new_text, (int)batch_id, false)) return false;
                    }
                    
                    bool all_done = true;
                    for (bool f : finished) if (!f) { all_done = false; break; }
                    return all_done;
                }
                
                // Normal token: Wait for complete UTF-8 characters
                if (new_text.find("\xef\xbf\xbd") == std::string::npos) {
                    previous_text[batch_id] = current_text;
                    if (!on_token(new_text, (int)batch_id, false)) return true;
                }
            }
            
            if (step_result.is_last) finished[batch_id] = true;
            return false;
        };
        
        std::vector<std::future<ctranslate2::GenerationResult>> futures = generator_->generate_batch_async(batch_tokens, options, 0, ctranslate2::BatchType::Examples);
        try {
            for (auto& f : futures) f.get();
        } catch (std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
    }
    
    std::string chat_completion(const std::string& prompt,
                                ctranslate2::GenerationOptions options,
                                int n,
                                const std::string& model_name) {
        
        std::vector<int> prompt_ids = tokenizer_->Encode(prompt);
        std::vector<std::string> prompt_tokens;
        prompt_tokens.reserve(prompt_ids.size());
        for (int id : prompt_ids) prompt_tokens.push_back(tokenizer_->IdToToken(id));
        
        // Duplicate prompt 'n' times
        std::vector<std::vector<std::string>> batch_tokens(n, prompt_tokens);
        
        // --- EARLY STOPPING CALLBACK FOR SYNC ROUTE ---
        std::vector<std::vector<int>> current_ids(n);
        std::vector<bool> finished(n, false);
        
        options.callback = [&](ctranslate2::GenerationStepResult step_result) {
            
            size_t batch_id = step_result.batch_id;
            if (finished[batch_id]) return true;
            
            current_ids[batch_id].push_back((int)step_result.token_id);
            std::string current_text = tokenizer_->Decode(current_ids[batch_id]);
            
            bool hit_stop = false;
            // Only check the suffix that could contain a newly completed stop word
            size_t max_sw_len = 20; // precompute once
            size_t check_from = current_text.size() > max_sw_len
                ? current_text.size() - max_sw_len : 0;
            std::string_view suffix(current_text.c_str() + check_from, current_text.size() - check_from);
            for (const auto& word : stop_words_) {
                if (suffix.find(word) != std::string_view::npos) { hit_stop = true; break; }
            }
            
            if (hit_stop || step_result.is_last) {
                finished[batch_id] = true;
            }
            
            // If ALL choices have hit a stop word, return false to ABORT compute!
            bool all_done = true;
            for (bool f : finished) {
                if (!f) all_done = false;
            }
            if (all_done) return true;
            
            return false; // Keep generating for remaining choices
        };
        
        // ----------------------------------------------
        
        std::vector<std::future<ctranslate2::GenerationResult>> futures = generator_->generate_batch_async(batch_tokens, options);
        
        Json::Value root(Json::objectValue);
        root["id"] = get_openai_style_id();
        root["object"] = "chat.completion";
        root["created"] = (Json::UInt64)get_created_timestamp();
        root["model"] = model_name;
        
        Json::Value choices(Json::arrayValue);
        
        Json::UInt64 completion_tokens = 0;
        // Loop through all 'n' choices returned by the future
        for (int i = 0; i < n; i++) {
            ctranslate2::GenerationResult result = futures[i].get();
            const auto& output_ids_size_t = result.sequences_ids[0];
            std::vector<int> output_ids(output_ids_size_t.begin(), output_ids_size_t.end());
            
            completion_tokens += output_ids.size();
            
            std::string response_text = tokenizer_->Decode(output_ids);
            
            // Strip the stop word from the final response
            for (const auto& word : stop_words_) {
                size_t pos = response_text.find(word);
                if (pos != std::string::npos) response_text = response_text.substr(0, pos);
            }
            
            Json::Value choice(Json::objectValue);
            choice["index"] = i;
            
            Json::Value message(Json::objectValue);
            message["role"] = "assistant";
            
            // --- QWEN TOOL CALL INTERCEPTION ---
            std::vector<ParsedToolCall> tool_calls_parsed;
            
            size_t start_tag = response_text.find("<tool_call>");
            size_t end_tag   = response_text.find("</tool_call>");
            
            if (start_tag != std::string::npos && end_tag != std::string::npos) {
                std::string json_str = response_text.substr(start_tag + 11,
                                                            end_tag - (start_tag + 11));
                tool_calls_parsed = parse_tool_call_json(json_str);
            }
            
            // --- BUILD RESPONSE ---
            if (!tool_calls_parsed.empty()) {
                message["content"] = Json::nullValue;

                Json::Value tool_calls_node(Json::arrayValue);
                for (int tc_idx = 0; tc_idx < (int)tool_calls_parsed.size(); ++tc_idx) {
                    Json::Value tc(Json::objectValue);
                    tc["id"]    = "call_" + get_openai_style_id();
                    tc["type"]  = "function";
                    tc["index"] = tc_idx;
                    Json::Value func(Json::objectValue);
                    func["name"]      = tool_calls_parsed[tc_idx].name;
                    func["arguments"] = tool_calls_parsed[tc_idx].arguments;
                    tc["function"] = func;
                    tool_calls_node.append(tc);
                }

                message["tool_calls"]    = tool_calls_node;
                choice["finish_reason"]  = "tool_calls";
            } else {
                message["content"]       = response_text;
                choice["finish_reason"]  = "stop";
            }
            
            choice["message"] = message;
            choices.append(choice);
        }
        
        root["choices"] = choices;
        
        Json::UInt64 prompt_tokens_count = (Json::UInt64)prompt_ids.size();
        Json::Value usage(Json::objectValue);
        usage["prompt_tokens"] = prompt_tokens_count;
        usage["completion_tokens"] = completion_tokens;
        usage["total_tokens"] = completion_tokens + prompt_tokens_count;
        root["usage"] = usage;
        
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        return Json::writeString(writer, root);
    }

private:
    std::unique_ptr<ctranslate2::Generator> generator_;
    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;
    inline static const std::vector<std::string> stop_words_ = {"<|im_end|>", "</s>", "<|endoftext|>", "<|eot_id|>", "<EOD>", "<end_of_turn>", "<eos>", "<|end_of_text|>",
                        "<|eom_id|>"};
#if CACHE_TOKEN_IDS
    std::vector<std::string> id_to_token_cache_;
#endif
};

class RerankerPipeline {
    public:
    RerankerPipeline(const std::string& model_dir,
                       int num_threads = 4,
                   const std::string& device = "cpu") {
        
        if (num_threads > 0) {
            ctranslate2::set_num_threads(num_threads);
        }
        
        tokenizer_ = LoadTokenizer(model_dir);
        if (!tokenizer_) throw std::runtime_error("No tokenizer found for RerankerPipeline");
        
        reranking_mode_ = LoadRerankingMode(model_dir);
        max_position_embeddings_ = LoadMaxPositionEmbeddings(model_dir);
        
        LoadSpecialTokenIds(model_dir,
                            reranking_mode_,
                            cls_id_,
                            sep_id_);
        
        LoadRerankHead(model_dir);
        
        try {
            ctranslate2::Device device_type = (device == "cuda") ?
                ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
            encoder_ = std::make_unique<ctranslate2::Encoder>(model_dir, device_type);
            
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load CTranslate2 Generator: " + std::string(e.what()));
        }
    }
    ctranslate2::StorageView forward_bert_reranker_batch(
                                                         const std::vector<std::vector<int64_t>>& batch_ids,
                                                         int64_t pad_id,
                                                         const std::vector<std::vector<size_t>>& batch_type_ids
                                                         ) {
        
        if (batch_ids.empty()) return {};
        
        size_t batch_size = batch_ids.size();
        
        // 1. Calculate Max Sequence Length
        size_t max_seq_len = 0;
        for (const auto& seq : batch_ids) {
            max_seq_len = std::max(max_seq_len, seq.size());
        }
        
        // 2. Prepare Flattened Buffers
        // Initialize with pad_id
        std::vector<int32_t> flattened_ids(batch_size * max_seq_len, (int32_t)pad_id);
        std::vector<int32_t> lengths;
        lengths.reserve(batch_size);
        
        for (size_t i = 0; i < batch_size; ++i) {
            lengths.push_back(static_cast<int32_t>(batch_ids[i].size()));
            for (size_t j = 0; j < batch_ids[i].size(); ++j) {
                flattened_ids[i * max_seq_len + j] = (int32_t)batch_ids[i][j];
            }
        }
        
        ctranslate2::StorageView ids_view({static_cast<int64_t>(batch_size),
            static_cast<int64_t>(max_seq_len)},
                                          flattened_ids,
                                          ctranslate2::Device::CPU);
        
        ctranslate2::StorageView lengths_view({static_cast<int64_t>(batch_size)},
                                              lengths,
                                              ctranslate2::Device::CPU);
        // 5. Forward Pass
        std::future<ctranslate2::EncoderForwardOutput> future;
        
        if (!batch_type_ids.empty()) {
            future = encoder_->forward_batch_async(ids_view, lengths_view, batch_type_ids);
        } else {
            future = encoder_->forward_batch_async(ids_view, lengths_view);
        }
        
        ctranslate2::EncoderForwardOutput enc_output = future.get();
        
        return enc_output.last_hidden_state.to(ctranslate2::Device::CPU);
    }
    
    std::string rerank_batch(const std::string& query, int top_n,
                             const std::vector<std::string>& documents) {
        
        std::vector<std::vector<std::string>> batch_input_tokens;
        std::vector<int> batch_original_indices;
        std::vector<std::vector<int64_t>> batch_ids;
        std::vector<std::vector<size_t>> batch_type_ids;
        
        // 1. Tokenize Query ONCE
        std::vector<int> q_ids = tokenizer_->Encode(query);
        
        switch (reranking_mode_) {
                case RERANKING_BERT:
                batch_ids.reserve(documents.size());
                batch_type_ids.reserve(documents.size());
                break;
            case RERANKING_ROBERTA:
            case RERANKING_LLM:
            default:
                batch_input_tokens.reserve(documents.size());
                batch_original_indices.reserve(documents.size());
                break;
        }
                
        for (size_t i = 0; i < documents.size(); ++i) {
            std::vector<int> doc_ids = tokenizer_->Encode(documents[i]);
            std::vector<int> ids;
            std::vector<size_t> type_ids;
            
            switch (reranking_mode_) {
                case RERANKING_MODERNBERT:
                    ids.reserve(q_ids.size() + doc_ids.size() + 3);
                    type_ids.reserve(ids.capacity());
                    ids.push_back(cls_id_); // <cls>
                    for(int x : q_ids) { ids.push_back(x); }
                    ids.push_back(sep_id_); // <sep>
                    for(int x : doc_ids) { ids.push_back(x); }
                    ids.push_back(sep_id_); // <sep>
                    type_ids.resize(ids.size(), 0);
                    break;
                case RERANKING_ROBERTA:
                    ids.reserve(q_ids.size() + doc_ids.size() + 4);
                    ids.push_back(cls_id_); // <s>
                    ids.insert(ids.end(), q_ids.begin(), q_ids.end());
                    ids.push_back(sep_id_); // </s>
                    ids.push_back(sep_id_); // </s>
                    ids.insert(ids.end(), doc_ids.begin(), doc_ids.end());
                    ids.push_back(sep_id_); // </s>
                    type_ids.resize(ids.size(), 0);
                    break;
                case RERANKING_BERT:
                    ids.reserve(q_ids.size() + doc_ids.size() + 3);
                    type_ids.reserve(ids.capacity());
                    ids.push_back(cls_id_); // [CLS]
                    type_ids.push_back(0);
                    for(int x : q_ids) { ids.push_back(x); type_ids.push_back(0); }
                    ids.push_back(sep_id_); // [SEP]
                    type_ids.push_back(0);
                    for(int x : doc_ids) { ids.push_back(x); type_ids.push_back(1); }
                    ids.push_back(sep_id_); // [SEP]
                    type_ids.push_back(1);
                    break;
                case RERANKING_LLM:
                default:
                    ids = tokenizer_->Encode(query + "\n" + documents[i]);
                    break;
            }
            
            if (ids.size() > max_position_embeddings_) {
                ids.resize(max_position_embeddings_ - 1);
                ids.push_back(sep_id_);
                
                if (!type_ids.empty()) {
                    type_ids.resize(max_position_embeddings_ - 1);
                    int end_type_id = (reranking_mode_ == RERANKING_BERT) ? 1 : 0;
                    type_ids.push_back(end_type_id);
                }
            }
            batch_ids.push_back(std::vector<int64_t>(ids.begin(), ids.end()));
            batch_type_ids.push_back(type_ids);
            batch_original_indices.push_back((int)i);
        }
        
        if (batch_ids.empty()) return "{\"results\": []}";
        
        ctranslate2::StorageView hidden_states;
        
        if(reranking_mode_ == RERANKING_BERT){
            hidden_states = forward_bert_reranker_batch(batch_ids, 0, batch_type_ids);
        }else{
            hidden_states = forward_bert_reranker_batch(batch_ids, 1, std::vector<std::vector<size_t>>());
        }
        
        // Access raw pointer
        const float* raw_data = hidden_states.data<float>();
        const auto& shape = hidden_states.shape();
        long batch_size = shape[0];
        long max_time = shape[1];
        long hidden_dim = shape[2];
        long stride_batch = max_time * hidden_dim;
        
        std::vector<RerankResult> results;
        results.reserve(batch_size);
        
        if(reranking_mode_ == RERANKING_BERT){
            std::vector<float> batch_logits;
            batch_logits.reserve(batch_size);
            for (long b = 0; b < batch_size; ++b) {
                float* batch_ptr = const_cast<float*>(raw_data + (b * stride_batch));
                Eigen::VectorXf embedding(hidden_dim);
                embedding = Eigen::Map<Eigen::VectorXf>(batch_ptr, hidden_dim);
                float logits = 0.0f;
                if (has_dense_layer_) {
                    Eigen::VectorXf dense_out = dense_weights_ * embedding + dense_bias_;
                    dense_out = dense_out.unaryExpr([](float x) { return std::tanh(x); });
                    logits = dense_out.dot(out_weights_) + out_bias_;
                }
                else {
                    logits = embedding.dot(out_weights_) + out_bias_;
                }
                // STORE RAW LOGIT INSTEAD OF APPLYING SIGMOID
                batch_logits.push_back(logits);
            }
            // 2. Apply Softmax over the batch of logits
            // Algorithm: Softmax(x_i) = exp(x_i) / sum(exp(x_j))
            // We subtract max_logit for numerical stability to prevent overflow
                        
//            float max_logit = -std::numeric_limits<float>::infinity();
            float max_logit = -3.402823466e+38f; // -FLT_MAX, safe under ffast-math

            for (float val : batch_logits) {
                if (val > max_logit) max_logit = val;
            }
            float sum_exp = 0.0f;
            std::vector<float> exps;
            exps.reserve(batch_size);
            for (float val : batch_logits) {
                float e = std::exp(val - max_logit);
                exps.push_back(e);
                sum_exp += e;
            }
            // 3. Populate Results with Normalized Scores
            for (long b = 0; b < batch_size; ++b) {
                float score = exps[b] / sum_exp; // This ensures sum(scores) == 1.0
                results.push_back({ batch_original_indices[b], score });
            }
                        
        }else{
            for (long b = 0; b < batch_size; ++b) {
                float* batch_ptr = const_cast<float*>(raw_data + (b * stride_batch));
                Eigen::VectorXf embedding(hidden_dim);
                embedding = Eigen::Map<Eigen::VectorXf>(batch_ptr, hidden_dim);
                float logits = 0.0f;
                if (has_dense_layer_) {
                    // Layer 1: Dense (Linear)
//                    Eigen::VectorXf dense_out = (embedding.transpose() * dense_weights_).transpose();
                    Eigen::VectorXf dense_out = dense_weights_ * embedding + dense_bias_;
                    // Layer 2: Activation (Tanh)
                    dense_out = dense_out.unaryExpr([](float x) { return std::tanh(x); });
                    // Layer 3: Out Projection (Linear)
                    logits = dense_out.dot(out_weights_) + out_bias_;
                }
                else {
                    // Simple Linear Head (MiniLM style)
                    logits = embedding.dot(out_weights_) + out_bias_;
                }
//                float score = 1.0f / (1.0f + std::exp(-logits));
                // Clamp to avoid exp overflow:
                float clamped = std::max(-88.0f, std::min(88.0f, -logits)); // exp overflows beyond ~88
                float score = 1.0f / (1.0f + std::exp(clamped));
                results.push_back({ batch_original_indices[b], score });
            }
        }
        
        // 6. Sort and Top-N
        auto sorter = [](const RerankResult& a, const RerankResult& b) {
            return a.score > b.score; // Descending
        };
        if (top_n > 0 && top_n < (int)results.size()) {
            // Partial sort is O(N * log(k)) - faster than full sort
            std::partial_sort(results.begin(), results.begin() + top_n, results.end(), sorter);
            results.resize(top_n);
        } else {
            std::sort(results.begin(), results.end(), sorter);
        }
        // 7. JSON Serialization
            Json::Value rootNode(Json::objectValue);
            Json::Value listNode(Json::arrayValue);
            
            for (const auto& result : results) {
                Json::Value dataNode = Json::objectValue;
                dataNode["index"] = result.index;
                dataNode["relevance_score"] = result.score;
                listNode.append(dataNode);
            }
            
            rootNode["results"] = listNode;
            rootNode["object"] = "list";
            
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            return Json::writeString(writer, rootNode);
    }
private:
    std::unique_ptr<ctranslate2::Encoder> encoder_;
    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;
   
    RerankingMode reranking_mode_;
    int max_position_embeddings_;
    
    int cls_id_ = 101;
    int sep_id_ = 102;
    
    // Head Weights
    bool has_dense_layer_ = false;
    Eigen::MatrixXf dense_weights_; // [Hidden, Hidden]
    Eigen::VectorXf dense_bias_;    // [Hidden]
    Eigen::VectorXf out_weights_;   // [Hidden]
    float out_bias_ = 0.0f;

    void LoadRerankHead(const std::string& model_dir) {
        fs::path bin_path = fs::path(model_dir) / "rerank_head.bin";
        std::ifstream file(bin_path.string(), std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("rerank_head.bin not found");
        
        // 1. Read Version
        int32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(int32_t));
        if (!file) throw std::runtime_error(" file is truncated or malformed");
        
        // 2. Read Hidden Dimension
        int32_t dim = 0;
        file.read(reinterpret_cast<char*>(&dim), sizeof(int32_t));
        if (!file) throw std::runtime_error(" file is truncated or malformed");
        
        if (version == 2) {
            has_dense_layer_ = true;
            
            std::vector<float> dense_w_buf(dim * dim);
            file.read(reinterpret_cast<char*>(dense_w_buf.data()), dense_w_buf.size() * sizeof(float));
            if (!file) throw std::runtime_error(" file is truncated or malformed");
            
            std::vector<float> dense_b_buf(dim);
            file.read(reinterpret_cast<char*>(dense_b_buf.data()), dim * sizeof(float));
            if (!file) throw std::runtime_error(" file is truncated or malformed");
            
            dense_weights_ = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>::Map(dense_w_buf.data(), dim, dim);
            
            dense_bias_ = Eigen::VectorXf::Map(dense_b_buf.data(), dim);
        }
        
        // 3. Read Out Layer
        file.read(reinterpret_cast<char*>(&out_bias_), sizeof(float));
        if (!file) throw std::runtime_error(" file is truncated or malformed");
        
        std::vector<float> out_w_buf(dim);
        file.read(reinterpret_cast<char*>(out_w_buf.data()), dim * sizeof(float));
        if (!file) throw std::runtime_error(" file is truncated or malformed");
        out_weights_ = Eigen::VectorXf::Map(out_w_buf.data(), dim);
    }
};

class TranslationPipeline {
public:
    TranslationPipeline(const std::string& model_dir,
                        int num_threads = 4,
                        const std::string& device = "cpu") {
        
        if (num_threads > 0) {
            ctranslate2::set_num_threads(num_threads);
        }
        
        translate_model_ = TranslateModel::TRANSLATE_UNKNOWN;
        
        fs::path sp_path = fs::path(model_dir) / "tokenizer.model";
        if(fs::exists(sp_path)) {
            tokenizer_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
            auto status = tokenizer_->Load(sp_path.string());
            if (!status.ok()) {
                throw std::runtime_error("TranslationPipeline: failed to load tokenizer: " + status.ToString());
            }
            translate_model_ = TranslateModel::TRANSLATE_BART;
        }

        fs::path sp_src_path = fs::path(model_dir) / "source.tokenizer.model";
        if(fs::exists(sp_src_path)) {
            tokenizer_src_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
            auto status = tokenizer_src_->Load(sp_src_path.string());
            if (!status.ok()) {
                throw std::runtime_error("TranslationPipeline: failed to load tokenizer: " + status.ToString());
            }
            translate_model_ = TranslateModel::TRANSLATE_MARIAN;
        }
        
        fs::path sp_tgt_path = fs::path(model_dir) / "target.tokenizer.model";
        if(fs::exists(sp_tgt_path)) {
            tokenizer_tgt_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
            auto status = tokenizer_tgt_->Load(sp_tgt_path.string());
            if (!status.ok()) {
                throw std::runtime_error("TranslationPipeline: failed to load tokenizer: " + status.ToString());
            }
        }
        
        if (translate_model_ == TRANSLATE_UNKNOWN)
            throw std::runtime_error("TranslationPipeline: failed to load tokenizer.");
        
        eos_token_ = "</s>";
        
        try {
            ctranslate2::Device device_type = (device == "cuda") ?
            ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
            translator_ = std::make_unique<ctranslate2::Translator>(model_dir, device_type);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load CTranslate2 Translator: " + std::string(e.what()));
        }
    }
    
    std::string translate_batch(const std::vector<std::string>& texts,
                                const std::string& src_lang,
                                const std::string& tgt_lang,
                                const ctranslate2::TranslationOptions& options) {
        std::vector<std::vector<std::string>> batch_tokens;
        std::vector<std::vector<std::string>> batch_prefixes;
        
        bool has_prefix = false;
        
        batch_tokens.reserve(texts.size());
        batch_prefixes.reserve(texts.size());
                
        for (const auto& text : texts) {
            std::vector<std::string> tokens;
            if (translate_model_ == TranslateModel::TRANSLATE_BART) {
                tokenizer_->Encode(text, &tokens);
            }else{
                tokenizer_src_->Encode(text, &tokens);
            }
            
            if(!src_lang.empty()) {
                tokens.insert(tokens.begin(), src_lang);
                has_prefix = true;
            }else if(!tgt_lang.empty()) {
                has_prefix = true;
            }else if (translate_model_ == TranslateModel::TRANSLATE_MARIAN) {
                has_prefix = true;
            }
            
            if (tokens.empty() || tokens.back() != eos_token_) {
                tokens.push_back(eos_token_);
            }
            
            batch_tokens.push_back(std::move(tokens));
            batch_prefixes.push_back({tgt_lang});  // force first decoded token
        }
        
        // Count prompt tokens before inference
        size_t prompt_tokens = 0;
        for (const auto& toks : batch_tokens) prompt_tokens += toks.size();
        
        auto results = translator_->translate_batch(batch_tokens, batch_prefixes, options);
        
        Json::Value rootNode(Json::objectValue);
        Json::Value translationsNode(Json::arrayValue);
        
        size_t completion_tokens = 0;
        for (size_t i = 0; i < results.size(); ++i) {
            const ctranslate2::TranslationResult result = results.at(i);
            Json::Value translationNode(Json::objectValue);
            if(result.scores.size() != 0) {
                translationNode["score"] = result.scores[0];
                float confidence = std::exp(result.scores[0]);
                translationNode["confidence"] = confidence;
            }else{
//                translationNode["score"] = Json::nullValue;
//                translationNode["confidence"] = Json::nullValue;
            }
            Json::Value hypothesesNode(Json::arrayValue);
            for (const auto& hyp : result.hypotheses) {
                auto tokens = hyp;
                if((has_prefix) && (!tokens.empty())) {
                    tokens.erase(tokens.begin());
                }
                completion_tokens += tokens.size(); // each hypothesis token vector
                std::string detokenized;
                if (translate_model_ == TranslateModel::TRANSLATE_BART) {
                    tokenizer_->Decode(tokens, &detokenized);
                } else {
                    tokenizer_tgt_->Decode(tokens, &detokenized);
                }
                hypothesesNode.append(detokenized);
            }
            translationNode["text"] = hypothesesNode;
            translationsNode.append(translationNode);
        }
        
        rootNode["translations"] = translationsNode;
        
        Json::Value usage(Json::objectValue);
        usage["prompt_tokens"]     = (Json::UInt64)prompt_tokens;
        usage["completion_tokens"] = (Json::UInt64)completion_tokens;
        usage["total_tokens"]      = (Json::UInt64)(prompt_tokens + completion_tokens);
        rootNode["usage"] = usage;
        
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        return Json::writeString(writer, rootNode);
    }
    
    // Streaming variant: calls on_token for each decoded token increment.
    // on_token(text, sentence_index) → return false to abort early.
    void translate_batch_stream(const std::vector<std::string>& texts,
                                const std::string& src_lang,
                                const std::string& tgt_lang,
                                ctranslate2::TranslationOptions options,
                                std::function<bool(const std::string&, int)> on_token,
                                bool use_sampling = false) {

        if (use_sampling) {
            // Real streaming — force beam=1, use callback
            options.beam_size = 1;
            
            // build batch_tokens exactly as you do now...
            std::vector<std::vector<std::string>> batch_tokens;
            std::vector<std::vector<std::string>> batch_prefixes;
            bool has_prefix = false;

            for (const auto& text : texts) {
                std::vector<std::string> tokens;
                if (translate_model_ == TranslateModel::TRANSLATE_BART)
                    tokenizer_->Encode(text, &tokens);
                else
                    tokenizer_src_->Encode(text, &tokens);

                if (!src_lang.empty()) { tokens.insert(tokens.begin(), src_lang); has_prefix = true; }
                else if (!tgt_lang.empty()) { has_prefix = true; }
                else if (translate_model_ == TranslateModel::TRANSLATE_MARIAN) { has_prefix = true; }

                if (tokens.empty() || tokens.back() != eos_token_)
                    tokens.push_back(eos_token_);

                batch_tokens.push_back(std::move(tokens));
                batch_prefixes.push_back({tgt_lang});
            }

            size_t n = texts.size();
            std::vector<std::vector<size_t>> current_ids(n);
            std::vector<std::vector<std::string>> current_tokens(n);
            std::vector<std::string> previous_text(n, "");
            std::vector<bool> finished(n, false);
            bool has_pref = has_prefix;

            options.callback = [&](ctranslate2::GenerationStepResult step) -> bool {
                size_t sid = step.batch_id;
                if (sid >= n || finished[sid]) return false;

                current_ids[sid].push_back(step.token_id);
                
                // Skip the prefix token on first step
                if (has_pref && current_ids[sid].size() == 1) {
                    return false;
                }
                                
                current_tokens[sid].push_back(step.token);
                std::vector<std::string> partial(current_tokens[sid].begin(), current_tokens[sid].end());
                
//                std::vector<int> id_ints(current_ids[sid].begin(), current_ids[sid].end());
                
                std::string current_text;
                if (translate_model_ == TranslateModel::TRANSLATE_BART) {
                    tokenizer_->Decode(partial, &current_text);
                }else{
                    tokenizer_tgt_->Decode(partial, &current_text);
                }
                
//                if (translate_model_ == TranslateModel::TRANSLATE_BART)
//                    tokenizer_->Decode(id_ints, &current_text);
//                else
//                    tokenizer_tgt_->Decode(id_ints, &current_text);

                if (current_text.length() > previous_text[sid].length()) {
                    if (current_text.find("\xef\xbf\xbd") == std::string::npos) {
                        std::string delta = current_text.substr(previous_text[sid].length());
                        previous_text[sid] = current_text;
                        if (!delta.empty())
                            if (!on_token(delta, (int)sid)) return true;
                    }
                }

                if (step.is_last) finished[sid] = true;

                bool all_done = true;
                for (bool f : finished) if (!f) { all_done = false; break; }
                return all_done;
            };

            auto futures = translator_->translate_batch_async(batch_tokens, batch_prefixes, options);
            try {
                for (auto& f : futures) f.get();
            } catch (std::exception& e) {
                std::cerr << e.what() << std::endl;
            }

        } else {
            
            std::vector<std::vector<std::string>> batch_tokens;
            std::vector<std::vector<std::string>> batch_prefixes;
            
            bool has_prefix = false;
            
            batch_tokens.reserve(texts.size());
            batch_prefixes.reserve(texts.size());
                    
            for (const auto& text : texts) {
                std::vector<std::string> tokens;
                if (translate_model_ == TranslateModel::TRANSLATE_BART) {
                    tokenizer_->Encode(text, &tokens);
                }else{
                    tokenizer_src_->Encode(text, &tokens);
                }
                
                if(!src_lang.empty()) {
                    tokens.insert(tokens.begin(), src_lang);
                    has_prefix = true;
                }else if(!tgt_lang.empty()) {
                    has_prefix = true;
                }
                
                if (tokens.empty() || tokens.back() != eos_token_) {
                    tokens.push_back(eos_token_);
                }
                
                batch_tokens.push_back(std::move(tokens));
                batch_prefixes.push_back({tgt_lang});  // force first decoded token
            }
            
            // Count prompt tokens before inference
            size_t prompt_tokens = 0;
            for (const auto& toks : batch_tokens) prompt_tokens += toks.size();
            
            auto results = translator_->translate_batch(batch_tokens, batch_prefixes, options);
            
            // Now fake-stream each result word by word
            
            for (size_t i = 0; i < results.size(); ++i) {
                if (results[i].hypotheses.empty()) continue;
                
                // Strip leading tgt lang token
                auto hyp = results[i].hypotheses[0];
                
                auto tokens = hyp;
                if((has_prefix) && (!tokens.empty())) {
                    tokens.erase(tokens.begin());
                }
                
                std::string accumulated;
                
                // Decode and stream token by token
                for (size_t t = 0; t < tokens.size(); ++t) {
                    // Decode tokens seen so far to get cumulative text
                    std::vector<std::string> partial(tokens.begin(), tokens.begin() + t + 1);
                    std::string current_text;
                    if (translate_model_ == TranslateModel::TRANSLATE_BART) {
                        tokenizer_->Decode(partial, &current_text);
                    }else{
                        tokenizer_tgt_->Decode(partial, &current_text);
                    }
                    
                    // Emit only the new suffix since last emission
                    if (current_text.length() > accumulated.length()) {
                        if (current_text.find("\xef\xbf\xbd") == std::string::npos) {
                            std::string delta = current_text.substr(accumulated.length());
                            accumulated = current_text;
                            if (!delta.empty()) {
                                if (!on_token(delta, (int)i)) goto next_sentence;
                            }
                        }
                    }
                }
                next_sentence:;
            }
        }
    }
    
private:
    std::unique_ptr<ctranslate2::Translator> translator_;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> tokenizer_;
    std::string eos_token_;

    std::unique_ptr<sentencepiece::SentencePieceProcessor> tokenizer_src_;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> tokenizer_tgt_;
    
    TranslateModel translate_model_;
    
};

class EmbeddingPipeline {
public:
    EmbeddingPipeline(const std::string& model_dir,
                      int num_threads = 4,
                      const std::string& device = "cpu") {
        
        // --- 1. Optimize Threading ---
        // This sets the number of threads used for matrix multiplication (intra-op).
        // If you set this to 4, it uses 4 cores for the math.
        if (num_threads > 0) {
            ctranslate2::set_num_threads(num_threads);
        }
        // --- 2. Load Tokenizer ---
        tokenizer_ = LoadTokenizer(model_dir);
        if (!tokenizer_) throw std::runtime_error("No tokenizer.json found");
        
        reranking_mode_ = LoadRerankingMode(model_dir);
        max_position_embeddings_ = LoadMaxPositionEmbeddings(model_dir);
        
        LoadSpecialTokenIds(model_dir,
                            reranking_mode_,
                            cls_id_,
                            sep_id_);
        
        try {
            ctranslate2::Device device_type = (device == "cuda") ?
            ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
            encoder_ = std::make_unique<ctranslate2::Encoder>(model_dir, device_type);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load CTranslate2 model: " + std::string(e.what()));
        }
    }
    
    // Helper to tokenize single string
    std::vector<int32_t> tokenize_one(const std::string& text) {
        return tokenizer_->Encode(text);
    }
    
    std::string embed_batch(const std::vector<std::string>& texts,
                            PoolingStrategy strategy,
                            bool l2_normalize = true) {
        
        if (tokenizer_ == nullptr || texts.empty()) {
            return "{\"object\":\"list\",\"data\":[]}";
        }
        
        // 1. Tokenize & Prepare Batch
        std::vector<std::vector<size_t>> batch_ids;
        batch_ids.reserve(texts.size());
        
        for (const auto& text : texts) {
            auto ids_int = tokenize_one(text);
            std::vector<size_t> ids_size_t;
            size_t input_len = ids_int.size();
            if (input_len > max_position_embeddings_ - 2)
                input_len = max_position_embeddings_ - 2;
            ids_size_t.reserve(input_len + 2);
            ids_size_t.push_back(cls_id_);
            ids_size_t.insert(ids_size_t.end(), ids_int.begin(), ids_int.begin() + input_len);
            ids_size_t.push_back(sep_id_);
            batch_ids.push_back(std::move(ids_size_t));
        }
        
        // 2. Forward Pass
        auto future = encoder_->forward_batch_async(batch_ids);
        ctranslate2::EncoderForwardOutput result = future.get();
        ctranslate2::StorageView hidden_states_cpu = result.last_hidden_state.to(ctranslate2::Device::CPU);
        
        // 3. Zero-Copy Setup
        float* raw_data = hidden_states_cpu.data<float>();
        const auto& shape = hidden_states_cpu.shape();
        long batch_size = shape[0];
        long max_seq_len = shape[1];
        long hidden_dim  = shape[2];
        long stride_batch = max_seq_len * hidden_dim;
        
        std::vector<float> all_embeddings(batch_size * hidden_dim);
        
        // 4. Compute Pooling (unchanged)
        for (long b = 0; b < batch_size; ++b) {
            long valid_len = batch_ids[b].size();
            if (valid_len == 0) valid_len = 1;
            Eigen::Map<Eigen::VectorXf> target_vec(all_embeddings.data() + (b * hidden_dim), hidden_dim);
            float* sentence_ptr = raw_data + (b * stride_batch);
            
            if (strategy == PoolingStrategy::MEAN) {
                Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
                    mat(sentence_ptr, valid_len, hidden_dim);
                target_vec.setZero();
                for (long i = 0; i < valid_len; ++i) target_vec += mat.row(i);
                target_vec /= static_cast<float>(valid_len);
            } else if (strategy == PoolingStrategy::CLS) {
                target_vec = Eigen::Map<Eigen::VectorXf>(sentence_ptr, hidden_dim);
            } else if (strategy == PoolingStrategy::LAST_TOKEN) {
                float* last_ptr = sentence_ptr + ((valid_len - 1) * hidden_dim);
                target_vec = Eigen::Map<Eigen::VectorXf>(last_ptr, hidden_dim);
            }
            if (l2_normalize) target_vec.normalize();
        }
        
        // 5. Direct string serialization — avoids JsonCpp tree overhead
        //    Pre-allocate: each float is at most 14 chars (e.g. "-1.23456789e-10,")
        //    Total budget: batch_size * hidden_dim * 14 + structural overhead
        std::string out;
        out.reserve(batch_size * hidden_dim * 12 + batch_size * 32 + 32);
        
        out += "{\"object\":\"list\",\"data\":[";
        
        char buf[32];
        for (long b = 0; b < batch_size; ++b) {
            if (b > 0) out += ',';
            out += "{\"index\":";
            snprintf(buf, sizeof(buf), "%ld", b);
            out += buf;
            out += ",\"embedding\":[";
            
            const float* emb = all_embeddings.data() + (b * hidden_dim);
            for (long i = 0; i < hidden_dim; ++i) {
                if (i > 0) out += ',';
                // "%.9g" gives full float precision and picks the shorter of
                // fixed vs scientific notation automatically
                snprintf(buf, sizeof(buf), "%.9g", emb[i]);
                out += buf;
            }
            
            out += "]}";
        }
        
        out += "]}";
        return out;
    }

private:
    std::unique_ptr<ctranslate2::Encoder> encoder_;
    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;

    RerankingMode reranking_mode_;
    int max_position_embeddings_;
    
    int cls_id_ = 101;
    int sep_id_ = 102;
};

#ifdef WIN32
static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();

    // Get required buffer size in characters (including null terminator)
    int size_needed = MultiByteToWideChar(
        CP_UTF8,       // Source is UTF-8
        0,             // Default flags
        str.c_str(),   // Source string
        -1,            // Null-terminated
        nullptr,       // No output buffer yet
        0              // Requesting size
    );

    if (size_needed <= 0) return std::wstring();

    // Allocate buffer
    std::wstring wstr(size_needed, 0);

    // Perform conversion
    MultiByteToWideChar(
        CP_UTF8,
        0,
        str.c_str(),
        -1,
        &wstr[0],
        size_needed
    );

    // Remove the extra null terminator added by MultiByteToWideChar
    if (!wstr.empty() && wstr.back() == '\0') {
        wstr.pop_back();
    }

    return wstr;
}

static std::string wchar_to_utf8(const wchar_t* wstr) {
    if (!wstr) return std::string();
    
    // Get required buffer size in bytes
    int size_needed = WideCharToMultiByte(
                                          CP_UTF8,            // convert to UTF-8
                                          0,                  // default flags
                                          wstr,               // source wide string
                                          -1,                 // null-terminated
                                          nullptr, 0,         // no output buffer yet
                                          nullptr, nullptr
                                          );
    
    if (size_needed <= 0) return std::string();
    
    // Allocate buffer
    std::string utf8str(size_needed, 0);
    
    // Perform conversion
    WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        wstr,
                        -1,
                        &utf8str[0],
                        size_needed,
                        nullptr,
                        nullptr
                        );
    
    // Remove the extra null terminator added by WideCharToMultiByte
    if (!utf8str.empty() && utf8str.back() == '\0') {
        utf8str.pop_back();
    }
    
    return utf8str;
}
#endif

#pragma mark -

static void usage(void)
{
    fprintf(stderr, "Usage:  ct2-server -s -e embedding_model -p port\n\n");
    fprintf(stderr, " -%c path     : %s\n", 'm' , "translation model");
    fprintf(stderr, " -%c path     : %s\n", 'e' , "embedding model (pooling=mean)");
    fprintf(stderr, " -%c path     : %s\n", 'r' , "reranker model");
    fprintf(stderr, " -%c path     : %s\n", 'a' , "generate model");
    fprintf(stderr, " -%c path     : %s\n", 'g' , "chat completion model");
    fprintf(stderr, " -%c path     : %s\n", 't' , "chat template");
    fprintf(stderr, " -%c          : %s\n", 'j' , "chat template from stdin");
    fprintf(stderr, " %c           : %s\n", 'l' , "pooling=last-token (Llama)");
    fprintf(stderr, " %c           : %s\n", 'c' , "pooling=cls (Qwen)");
    fprintf(stderr, " %c           : %s\n", 's' , "server (OpenAI compatible endpoint)");
    fprintf(stderr, " %c           : %s\n", 'p' , "server listening port (default=8080)");
    fprintf(stderr, " %c           : %s\n", 'h' , "server host (default=127.0.0.1)  ");
    fprintf(stderr, " -%c path     : %s\n", 'i' , "input");
    fprintf(stderr, " %c           : %s\n", '-' , "use stdin for input");
    fprintf(stderr, " -%c path     : %s\n", 'o' , "output (default=stdout)");
    //
    exit(1);
}

extern OPTARG_T optarg;
extern int optind, opterr, optopt;

#ifdef WIN32
OPTARG_T optarg = 0;
int opterr = 1;
int optind = 1;
int optopt = 0;
int getopt(int argc, OPTARG_T *argv, OPTARG_T opts) {
    
    static int sp = 1;
    register int c;
    register OPTARG_T cp;
    
    if(sp == 1)
        if(optind >= argc ||
           argv[optind][0] != '-' || argv[optind][1] == '\0')
            return(EOF);
        else if(wcscmp(argv[optind], L"--") == NULL) {
            optind++;
            return(EOF);
        }
    optopt = c = argv[optind][sp];
    if(c == ':' || (cp=wcschr(opts, c)) == NULL) {
        ERR(L": illegal option -- ", c);
        if(argv[optind][++sp] == '\0') {
            optind++;
            sp = 1;
        }
        return('?');
    }
    if(*++cp == ':') {
        if(argv[optind][sp+1] != '\0')
            optarg = &argv[optind++][sp+1];
        else if(++optind >= argc) {
            ERR(L": option requires an argument -- ", c);
            sp = 1;
            return('?');
        } else
            optarg = argv[optind++];
        sp = 1;
    } else {
        if(argv[optind][++sp] == '\0') {
            sp = 1;
            optind++;
        }
        optarg = NULL;
    }
    return(c);
}
#define ARGS (OPTARG_T)L"m:e:r:g:a:i:o:sp:jt:bcld-jh"
#define _atoi _wtoi
#define _atof _wtof
#else
#define ARGS "m:e:r:g:a:i:o:sp:jt:bcld-jh"
#define _atoi atoi
#define _atof atof
#endif

#pragma mark -

namespace fs = std::filesystem;
static std::string get_model_name(std::string model_path) {
    // 1. Create a path object
    fs::path path(model_path);
    
    // 2. Handle trailing slashes (e.g., "models/phi-3/")
    // If the path ends in a separator, filename() might return empty.
    if (path.filename().empty()) {
        path = path.parent_path();
    }
    
    // 3. Return the folder/filename
    // .filename() returns "phi-3.onnx" (with extension)
    // .stem() returns "phi-3" (removes extension)
    return path.filename().string();
}

// Simple stable FNV-1a hash implementation
static std::string get_system_fingerprint(const std::string& model_path, const std::string& provider) {
    std::string identifier = model_path + "_" + provider;
    uint64_t hash = 14695981039346656037ULL;
    for (char c : identifier) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    std::stringstream ss;
    ss << "fp_" << std::hex << hash;
    return ss.str();
}

#pragma mark -

static void parse_request_generate(const std::string& property_name,
                                   const std::string& json_str,
                                   std::vector<std::string>& prompts,
                                   ctranslate2::TranslationOptions& options,
                                   std::string& src_lang,
                                   std::string& tgt_lang,
                                   bool& is_stream,
                                   bool& use_sampling) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    bool ok = reader->parse(json_str.c_str(), json_str.c_str() + json_str.size(), &root, &errors);
    if (!ok || !root.isObject()) return;

    // "prompt" or "input" : string or array of strings
    Json::Value prompt_node = root[property_name];
    if (prompt_node.isString()) {
        prompts.push_back(prompt_node.asString());
    } else if (prompt_node.isArray()) {
        for (auto& v : prompt_node) {
            if (v.isString()) prompts.push_back(v.asString());
        }
    }
    
    // BART, NLLB
    Json::Value from_lang_node = root["from"];
    if (from_lang_node.isString()) {
        src_lang = from_lang_node.asString();
    }
    
    Json::Value to_lang_node = root["to"];
    if (to_lang_node.isString()) {
        tgt_lang = to_lang_node.asString();
    }

    // "input"  : v1/translate uses ctranslate2::TranslationOptions
    // "prompt" : v1/generate  uses ctranslate2::TranslationOptions
    if (root["beam_size"].isNumeric())
        options.beam_size = root["beam_size"].asUInt();
    if (root["patience"].isNumeric())
        options.patience = root["patience"].asDouble();
    if (root["length_penalty"].isNumeric())
        options.length_penalty = root["length_penalty"].asFloat();
    if (root["coverage_penalty"].isNumeric())
        options.coverage_penalty = root["coverage_penalty"].asFloat();
    if (root["repetition_penalty"].isNumeric())
        options.repetition_penalty = root["repetition_penalty"].asFloat();
    if (root["no_repeat_ngram_size"].isNumeric())
        options.no_repeat_ngram_size = root["no_repeat_ngram_size"].asUInt();
    if (root["disable_unk"].isBool())
        options.disable_unk = root["disable_unk"].asBool();
    if (root["prefix_bias_beta"].isNumeric())
        options.prefix_bias_beta = root["prefix_bias_beta"].asFloat();
    if (root["return_end_token"].isBool())
        options.return_end_token = root["return_end_token"].asBool();
    if (root["max_input_length"].isNumeric())
        options.max_input_length = root["max_input_length"].asUInt();
    if (root["max_decoding_length"].isNumeric())
        options.max_decoding_length = root["max_decoding_length"].asUInt();
    if (root["min_decoding_length"].isNumeric())
        options.min_decoding_length = root["min_decoding_length"].asUInt();
    if (root["sampling_topk"].isNumeric())
        options.sampling_topk = root["sampling_topk"].asUInt();
    if (root["sampling_topp"].isNumeric())
        options.sampling_topp = root["sampling_topp"].asFloat();
    if (root["sampling_temperature"].isNumeric())
        options.sampling_temperature = root["sampling_temperature"].asFloat();
    if (root["use_vmap"].isBool())
        options.use_vmap = root["use_vmap"].asBool();
    if (root["num_hypotheses"].isNumeric())
        options.num_hypotheses = root["num_hypotheses"].asUInt();
    if (root["return_alternatives"].isBool())
        options.return_alternatives = root["return_alternatives"].asBool();
    if (root["min_alternative_expansion_prob"].isNumeric())
        options.min_alternative_expansion_prob = root["min_alternative_expansion_prob"].asFloat();
    if (root["replace_unknowns"].isBool())
        options.replace_unknowns = root["replace_unknowns"].asBool();
    
    if (root["stream"].isBool())
        is_stream = root["stream"].asBool();
    
    if (root["sampling"].isBool())
        use_sampling = root["sampling"].asBool();
}

static void parse_request_reranking(const std::string &json,
                                     std::string &query,
                                     int *top_n,
                                     std::vector<std::string> &documents
                                     ) {
    
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    bool parse = reader->parse(json.c_str(),
                               json.c_str() + json.size(),
                               &root,
                               &errors);
    
    if(parse)
    {
        if(root.isObject())
        {
            Json::Value query_node = root["query"];
            if(query_node.isString())
            {
                query = query_node.asString();
            }
            Json::Value top_n_node = root["top_n"];
            if(top_n_node.isNumeric())
            {
                *top_n = top_n_node.asInt();
            }
            
            Json::Value documents_node = root["documents"];
            if(documents_node.isArray())
            {
                for(Json::Value::const_iterator it = documents_node.begin() ; it != documents_node.end() ; it++)
                {
                    if(it->isString())
                    {
                        std::string document = it->asString();
                        documents.push_back(document);
                    }
                }
            }
        }
    }
}

static void parse_request_contextualized_embeddings(const std::string &json,
                                     std::vector<std::string> &inputs) {
    
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    bool parse = reader->parse(json.c_str(),
                               json.c_str() + json.size(),
                               &root,
                               &errors);
    
    if(parse && root.isObject())
    {
        // Voyage AI uses "inputs" (plural)
        Json::Value inputs_node = root["inputs"];
        
        // fallback for 4D AI Kit which uses "inout" (singular)
        inputs_node = inputs_node.isArray() ? inputs_node : root["input"];
        
        if(inputs_node.isArray())
        {
            // Iterate over documents (each document is an array of chunks)
            for (Json::Value::const_iterator it_doc = inputs_node.begin(); it_doc != inputs_node.end(); ++it_doc)
            {
                const Json::Value& chunk_array = *it_doc;
                if(chunk_array.isArray())
                {
                    // 1. Reconstruct the full document by concatenating its chunks
                    std::string full_document;
                    for (Json::Value::const_iterator it_chunk = chunk_array.begin(); it_chunk != chunk_array.end(); ++it_chunk)
                    {
                        if(it_chunk->isString()) {
                            full_document += it_chunk->asString();
                        }
                    }
                    
                    // 2. Flatten the request: create a contextualized input for each chunk
                    for (Json::Value::const_iterator it_chunk = chunk_array.begin(); it_chunk != chunk_array.end(); ++it_chunk)
                    {
                        if(it_chunk->isString()) {
                            std::string chunk = it_chunk->asString();
                            // Prepend the reconstructed document context to the specific chunk.
                            // This allows standard ONNX models to approximate Voyage's context-awareness.
                            inputs.push_back(full_document + "\n\n" + chunk);
                        }
                    }
                }
            }
        }
    }
}

static void parse_request_embeddings(const std::string &json,
                                     std::vector<std::string> &inputs) {
    
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    bool parse = reader->parse(json.c_str(),
                               json.c_str() + json.size(),
                               &root,
                               &errors);
    
    if(parse)
    {
        if(root.isObject())
        {
            Json::Value input_node = root["input"];
            if(input_node.isString())
            {
                inputs.push_back(input_node.asString());
            }
            if(input_node.isArray())
            {
                for (Json::ValueIterator i = input_node.begin(); i != input_node.end(); ++i)
                {
                    const auto& node = *i;
                    if(node.isString())
                    {
                        inputs.push_back(node.asString());
                    }
                }
            }
        }
    }
}

static void before_run_reranking(
                                 const std::string& request_body,
                                 std::string &query,
                                 int *top_n,
                                 std::vector<std::string> &documents
                                 ) {
    parse_request_reranking(request_body, query, top_n, documents);
}

static void before_run_contextualized_embeddings(
                                  const std::string& request_body,
                                  std::vector<std::string> &inputs
                                  ) {
    parse_request_contextualized_embeddings(request_body, inputs);
}

static void before_run_embeddings(
                                  const std::string& request_body,
                                  std::vector<std::string> &inputs
                                  ) {
    parse_request_embeddings(request_body, inputs);
}

#pragma mark -

static void send_error(httplib::Response& res, const std::string& message, int status = 400) {
    Json::Value root(Json::objectValue), err(Json::objectValue);
    err["message"] = message;
    err["type"] = "invalid_request_error";
    err["param"] = Json::nullValue;
    err["code"] = Json::nullValue;
    root["error"] = err;
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    res.set_content(Json::writeString(w, root), "application/json");
    res.status = status;
    std::cerr << "[Server] Error: " << message << std::endl;
}

int main(int argc, OPTARG_T argv[]) {
    
#ifdef WIN32
    std::wstring model_path_u16;
    std::wstring embedding_model_path_u16;
    std::wstring source_sp_path_u16;
    std::wstring reranker_model_path_u16;
    std::wstring chat_model_path_u16;
    std::wstring generate_model_path_u16;
#endif
    std::string model_path;           // -m
    std::string embedding_model_path; // -e
    std::string reranker_model_path;  // -r
    std::string chat_template;        // -j
    std::string chat_model_path;      // -g
    std::string generate_model_path;  // -a
    
    std::string generate_chat_template;
    OPTARG_T input_path  = NULL;      // -i
    OPTARG_T output_path = NULL;      // -o
    OPTARG_T chat_template_path = NULL;
    std::string source_sp_path;       // -f
    
    PoolingStrategy pooling_mode = PoolingStrategy::MEAN;
    
    // Server mode flags
    bool server_mode = false;         // -s
    int port = 8080;                  // -p
    std::string host = "127.0.0.1";   // -h
    
    std::vector<unsigned char> cli_request_json(0);
    
    int ch;
    
    while ((ch = getopt(argc, argv, ARGS)) != -1) {
        switch (ch){
            case 'm':
#ifdef WIN32
                model_path_u16 = optarg;
                model_path = wchar_to_utf8(model_path_u16.c_str());
#else
                model_path = optarg;
#endif
                break;
            case 'e':
#ifdef WIN32
                embedding_model_path_u16 = optarg;
                embedding_model_path = wchar_to_utf8(embedding_model_path_u16.c_str());
#else
                embedding_model_path = optarg;
#endif
                break;
            case 'r':
#ifdef WIN32
                reranker_model_path_u16 = optarg;
                reranker_model_path = wchar_to_utf8(reranker_model_path_u16.c_str());
#else
                reranker_model_path = optarg;
#endif
                break;
            case 'a':
#ifdef WIN32
                generate_model_path_u16 = optarg;
                generate_model_path = wchar_to_utf8(source_sp_path_u16.c_str());
#else
                generate_model_path = optarg;
#endif
                break;
            case 'g':
            #ifdef WIN32
                chat_model_path_u16 = optarg;
                chat_model_path = wchar_to_utf8(chat_model_path_u16.c_str());
            #else
                chat_model_path = optarg;
            #endif
                break;
            case 'i':
                input_path = optarg;
                break;
            case 'o':
                output_path = optarg;
                break;
            case 's':
                server_mode = true;
                break;
            case 'p':
                port = std::stoi(optarg);
                break;
            case 'b':
//                pooling_mode = POOLING_COLBERT;
                break;
            case 'c':
                pooling_mode = PoolingStrategy::CLS;
                break;
            case 'l':
                pooling_mode = PoolingStrategy::LAST_TOKEN;
                break;
            case 'd':
//                pooling_mode = POOLING_E2E;
                break;
            case 'h':
#ifdef WIN32
                host = wchar_to_utf8(optarg);
#else
                host = optarg;
#endif
                break;
            case 'j':
            case '-':
            {
                // Only relevant for CLI mode
                std::vector<uint8_t> buf(BUFLEN);
                size_t s;
                while ((s = fread(buf.data(), 1, buf.size(), stdin)) > 0) {
                    cli_request_json.insert(cli_request_json.end(), buf.begin(), buf.begin() + s);
                }
                if(ch == 'j') {
                    chat_template = std::string((const char *)cli_request_json.data(), cli_request_json.size());
                }
            }
                break;
            case 't':
            {
                chat_template_path = optarg;
                if (chat_template_path != NULL){
                    FILE *f = _fopen(chat_template_path, _rb);
                    if(f) {
                        std::vector<unsigned char> chat_template_string(0);
                        fseek(f, 0, SEEK_END);
                        size_t len = (size_t)ftell(f);
                        fseek(f, 0, SEEK_SET);
                        chat_template_string.resize(len);
                        fread(chat_template_string.data(), 1, chat_template_string.size(), f);
                        fclose(f);
                        chat_template = std::string((const char *)chat_template_string.data(), chat_template_string.size());
                    }
                }
            }
                break;
            default:
                usage();
                break;
        }
    }
    
    int intra_op_threads = GetOptimalIntraOpThreads();
    std::cout << "Detected " << intra_op_threads << " Intra-Op threads." << std::endl;
    
    std::string fingerprint;
    long long model_created = 0;
    std::string modelName;
    
    std::unique_ptr<TranslationPipeline> translation_pipeline;
    
    //-m:translation
    if (model_path.length() != 0) {
        if (fs::exists(model_path)) {
            if (fs::is_directory(model_path)) {
                // 1.a Initialize Model and Tokenizer (Load once)
                std::cerr << "[Translate] Loading from " << model_path << std::endl;
                fingerprint = get_system_fingerprint(model_path, "directml");
                modelName = get_model_name(model_path);
                try {
#ifdef WIN32
                    modelName = get_model_name(wchar_to_utf8(fs::path(model_path).c_str()));
#else
                    modelName = get_model_name(fs::path(model_path));
#endif
                    translation_pipeline = std::make_unique<TranslationPipeline>(model_path, intra_op_threads);
                    model_created = get_created_timestamp();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to load model: " << e.what() << std::endl;
                    return 1;
                }
            }
        }
    }
    
    std::string embedding_fingerprint;
    long long embedding_model_created = 0;
    std::string embedding_modelName;

    std::unique_ptr<EmbeddingPipeline> pipeline;
    
    //-e:embeddings
    if (embedding_model_path.length() != 0) {
        if (fs::exists(embedding_model_path)) {
            if (fs::is_directory(embedding_model_path)) {
                // 1.b Initialize Embedding and Session (Load once)
                std::cerr << "[Embedding] Loading from " << embedding_model_path << std::endl;
                embedding_fingerprint = get_system_fingerprint(embedding_model_path, "directml");
                try {
#ifdef WIN32
                    embedding_modelName = get_model_name(wchar_to_utf8(fs::path(embedding_model_path).c_str()));
#else
                    embedding_modelName = get_model_name(fs::path(embedding_model_path));
#endif
                    pipeline = std::make_unique<EmbeddingPipeline>(embedding_model_path, intra_op_threads);
                    embedding_model_created = get_created_timestamp();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to load model: " << e.what() << std::endl;
                    return 1;
                }
            }
        }
    }
    
    std::string reranking_fingerprint;
    long long reranking_model_created = 0;
    std::string reranking_modelName;

    std::unique_ptr<RerankerPipeline> rerank_pipeline;
    
    //-r:rerank
    if (reranker_model_path.length() != 0) {
        if (fs::exists(reranker_model_path)) {
            if (fs::is_directory(reranker_model_path)) {
                // 1.b Initialize Rerank and Session (Load once)
                std::cerr << "[Rerank] Loading from " << reranker_model_path << std::endl;
                reranking_fingerprint = get_system_fingerprint(reranker_model_path, "directml");
                try {
#ifdef WIN32
                    reranking_modelName = get_model_name(wchar_to_utf8(fs::path(reranker_model_path).c_str()));
#else
                    reranking_modelName = get_model_name(fs::path(reranker_model_path));
#endif
                    rerank_pipeline = std::make_unique<RerankerPipeline>(reranker_model_path, intra_op_threads);
                    reranking_model_created = get_created_timestamp();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to load model: " << e.what() << std::endl;
                    return 1;
                }
            }
        }
    }
    
    std::string generate_fingerprint;
    long long generate_model_created = 0;
    std::string generate_modelName;
    
    std::unique_ptr<GeneratePipeline> generate_pipeline;
    
    //-a:generate
    if (generate_model_path.length() != 0) {
        if (fs::exists(generate_model_path) && fs::is_directory(generate_model_path)) {
            std::cerr << "[Generator] Loading from " << generate_model_path << std::endl;
            generate_fingerprint = get_system_fingerprint(generate_model_path, "directml");
            try {
    #ifdef WIN32
                generate_modelName = get_model_name(wchar_to_utf8(fs::path(generate_model_path).c_str()));
    #else
                generate_modelName = get_model_name(fs::path(generate_model_path));
    #endif
                generate_pipeline = std::make_unique<GeneratePipeline>(generate_model_path, intra_op_threads);
                generate_chat_template = LoadChatTemplate(generate_model_path);
                generate_model_created = get_created_timestamp();
            } catch (const std::exception& e) {
                std::cerr << "Failed to load generate model: " << e.what() << std::endl;
                return 1;
            }
        }
    }
    
    std::string chat_fingerprint;
    long long chat_model_created = 0;
    std::string chat_modelName;
    
    std::unique_ptr<ChatPipeline> chat_pipeline;
    
    //-g:chat
    if (chat_model_path.length() != 0) {
        if (fs::exists(chat_model_path) && fs::is_directory(chat_model_path)) {
            std::cerr << "[Chat] Loading from " << chat_model_path << std::endl;
            chat_fingerprint = get_system_fingerprint(chat_model_path, "directml");
            try {
    #ifdef WIN32
                chat_modelName = get_model_name(wchar_to_utf8(fs::path(chat_model_path).c_str()));
    #else
                chat_modelName = get_model_name(fs::path(chat_model_path));
    #endif
                chat_pipeline = std::make_unique<ChatPipeline>(chat_model_path, intra_op_threads);
                if(chat_template == "") {
                    chat_template = LoadChatTemplate(chat_model_path);
                }
                chat_model_created = get_created_timestamp();
            } catch (const std::exception& e) {
                std::cerr << "Failed to load chat model: " << e.what() << std::endl;
                return 1;
            }
        }
    }
    
    // ---------------------------------------------------------
    // SERVER MODE
    // ---------------------------------------------------------
    if (server_mode) {
        httplib::Server svr;
        
        // Route: /v1/models
        svr.Get("/v1/models", [&](const httplib::Request& req, httplib::Response& res) {
            std::cout << "[Server] /v1/models request received." << std::endl;
            /*
             The model object
             https://platform.openai.com/docs/api-reference/models/object
             */
            // Create the list wrapper
            Json::Value root(Json::objectValue);
            root["object"] = "list";
            root["data"] = Json::Value(Json::arrayValue);
            // Create the model object
            if(model_created != 0) {
                Json::Value modelCard(Json::objectValue);
                modelCard["id"] = modelName;
                modelCard["object"] = "model";
                modelCard["created"] = model_created;
                modelCard["owned_by"] = "system";
                root["data"].append(modelCard);
            }
            if(embedding_model_created != 0) {
                Json::Value modelCard(Json::objectValue);
                modelCard["id"] = embedding_modelName;
                modelCard["object"] = "model";
                modelCard["created"] = embedding_model_created;
                modelCard["owned_by"] = "system";
                root["data"].append(modelCard);
            }
            if(reranking_model_created != 0) {
                Json::Value modelCard(Json::objectValue);
                modelCard["id"] = reranking_modelName;
                modelCard["object"] = "model";
                modelCard["created"] = reranking_model_created;
                modelCard["owned_by"] = "system";
                root["data"].append(modelCard);
            }
            if(generate_model_created != 0) {
                Json::Value modelCard(Json::objectValue);
                modelCard["id"] = generate_modelName;
                modelCard["object"] = "model";
                modelCard["created"] = generate_model_created;
                modelCard["owned_by"] = "system";
                root["data"].append(modelCard);
            }
            // Serialize
            Json::StreamWriterBuilder writer;
            writer["indentation"] = ""; // Minified JSON
            std::string json_str = Json::writeString(writer, root);
            // Respond
            res.set_content(json_str, "application/json");
            res.status = 200;
        });
        
        auto chat_completions_handler = [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/chat/completions request received." << std::endl;
            
            try {
                
                if (generate_model_created == 0) {
                    throw std::invalid_argument("[Generator] Model not loaded. Pass model directory via -g");
                }
                
                std::string prompt = "";
                ctranslate2::GenerationOptions options;
                bool is_stream = false;
                int n = 1;
                bool has_tools = false;
                
                parse_request_chat_completion(req.body, prompt, chat_template, options, is_stream, n, has_tools);
                
                if (prompt.empty()) {
                    throw std::invalid_argument("Request must contain a valid 'messages' array.");
                }
                
                // --- STREAMING MODE ---
                if (is_stream) {
                    // 1. Extract raw pointers safely
                    ChatPipeline* raw_chat_pipeline = chat_pipeline.get();
                    std::string req_id = get_openai_style_id();
                    
                    // 2. Pass everything by value (copy) or safe pointer
                    res.set_chunked_content_provider("text/event-stream",
                                                     [raw_chat_pipeline, prompt, options, generate_modelName, req_id, n, has_tools](size_t offset, httplib::DataSink &sink) {
                        
                        // 3. Callback to handle tokens as they are generated
                        auto token_callback = [&](const std::string& token, int choice_index, bool is_tool) {
                            Json::Value root(Json::objectValue);
                            root["id"] = req_id;
                            root["object"] = "chat.completion.chunk";
                            root["created"] = (Json::UInt64)get_created_timestamp();
                            root["model"] = generate_modelName;
                            Json::Value choices(Json::arrayValue);
                            Json::Value choice(Json::objectValue);
                            choice["index"] = choice_index;
                            Json::Value delta(Json::objectValue);
                            std::string tool_name = "";
                            std::string tool_args = "";
                            if(is_tool) {
                                try {
                                    nlohmann::json tool_json = nlohmann::json::parse(token);
                                    if (tool_json.contains("name") && tool_json.contains("arguments")) {
                                        tool_name = tool_json["name"].get<std::string>();
                                        
                                        if (tool_json["arguments"].is_object()) {
                                            tool_args = tool_json["arguments"].dump();
                                        } else {
                                            tool_args = tool_json["arguments"].get<std::string>();
                                        }
                                    }
                                }catch (...) {
                                    is_tool = false;
                                }
                            }
                            if (is_tool) {
                                std::vector<ParsedToolCall> tool_calls_parsed = parse_tool_call_json(token);
                                if (tool_calls_parsed.empty()) {
                                    is_tool = false;
                                } else {
                                    delta["content"] = Json::nullValue;

                                    Json::Value tool_calls_node(Json::arrayValue);
                                    for (int tc_idx = 0; tc_idx < (int)tool_calls_parsed.size(); ++tc_idx) {
                                        Json::Value tc(Json::objectValue);
                                        tc["id"]    = "call_" + get_openai_style_id();
                                        tc["type"]  = "function";
                                        tc["index"] = tc_idx;
                                        Json::Value func(Json::objectValue);
                                        func["name"]      = tool_calls_parsed[tc_idx].name;
                                        func["arguments"] = tool_calls_parsed[tc_idx].arguments;
                                        tc["function"] = func;
                                        tool_calls_node.append(tc);
                                    }

                                    delta["tool_calls"]     = tool_calls_node;
                                    choice["finish_reason"] = "tool_calls";
                                }
                            }
                            
                            if (!is_tool) {
                                delta["content"]        = token;
                                choice["finish_reason"] = Json::nullValue;
                            }
                            
                            choice["delta"] = delta;
                            
                            choices.append(choice);
                            root["choices"] = choices;
                            
                            Json::StreamWriterBuilder writer;
                            writer["indentation"] = "";
                            std::string chunk = "data: " + Json::writeString(writer, root) + "\n\n";
                            
                            // Write immediately to the client
                            sink.write(chunk.data(), chunk.size());
                            
                            return true; // Keep going
                        };
                        
                        // 4. Run inference synchronously. This blocks until the AI is completely done.
                        raw_chat_pipeline->chat_completion_stream(prompt, options, n, has_tools, token_callback);
                        
                        // 5. Send [DONE] to close the stream for the client
                        std::string done = "data: [DONE]\n\n";
                        sink.write(done.data(), done.size());
                        
                        sink.done(); // Close the connection
                        return false;
                    }
                                                     );
                }else{
                    std::string response_json = chat_pipeline->chat_completion(prompt, options, n, generate_modelName);
                    
                    res.set_content(response_json, "application/json");
                    res.status = 200;
                }

            } catch (const std::exception& e) {
                send_error(res, e.what(), 400);
            }
        };
        auto generate_handler = [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/generate request received." << std::endl;

            try {
                if (!generate_pipeline) {
                    throw std::invalid_argument("[Generate] pipeline not loaded. Pass a model with tokenizer.model via -g");
                }

                std::vector<std::string> prompts;
                std::string src_lang;
                std::string tgt_lang;
                ctranslate2::TranslationOptions options;
                bool is_stream = false;
                bool use_sampling = false;
                options.return_scores = true;

                parse_request_generate("prompt",
                                       req.body,
                                       prompts,
                                       options,
                                       src_lang,
                                       tgt_lang,
                                       is_stream,
                                       use_sampling);

                if (prompts.empty()) {
                    throw std::invalid_argument("'prompt' field must be a non-empty string or array of strings.");
                }

                if (is_stream) {
                    
                    options.beam_size = 1;  // force for real streaming
                    options.sampling_temperature = options.sampling_temperature > 0 ? options.sampling_temperature : 0.7f;
                    options.repetition_penalty = options.repetition_penalty > 0 ? options.repetition_penalty : 1.3f;
                    options.no_repeat_ngram_size = options.no_repeat_ngram_size > 0 ? options.no_repeat_ngram_size : 4;
                    options.max_decoding_length = options.max_decoding_length > 0 ? options.max_decoding_length : 256;
                    options.min_decoding_length = options.min_decoding_length > 0 ? options.min_decoding_length : 4;
                    options.sampling_topk = options.sampling_topk > 0 ? options.sampling_topk : 20;
                    options.sampling_topp = options.sampling_topp > 0 ? options.sampling_topp : 0.9f;
                    
                    // --- STREAMING MODE ---
                    GeneratePipeline* raw = generate_pipeline.get();
                    std::string mdl = generate_modelName;
                    size_t num_prompts = prompts.size();

                    res.set_chunked_content_provider("text/event-stream",
                        [raw, prompts, options, mdl, num_prompts](size_t, httplib::DataSink& sink) {

                        size_t prompt_toks = 0, completion_toks = 0;

                        auto token_callback = [&](const std::string& delta, int idx) -> bool {
                            Json::Value root(Json::objectValue);
                            root["object"] = "generate.chunk";
                            root["model"]  = mdl;
                            Json::Value arr(Json::arrayValue);
                            Json::Value node(Json::objectValue);
                            node["index"] = idx;
                            node["delta"] = delta;
                            arr.append(node);
                            root["results"] = arr;

                            Json::StreamWriterBuilder w;
                            w["indentation"] = "";
                            std::string chunk = "data: " + Json::writeString(w, root) + "\n\n";
                            sink.write(chunk.data(), chunk.size());
                            return true;
                        };

                        raw->generate_batch_stream(prompts, options, token_callback, prompt_toks, completion_toks);

                        // Send final usage chunk before [DONE]
                        Json::Value usage_root(Json::objectValue);
                        usage_root["object"] = "generate.chunk";
                        usage_root["model"]  = mdl;
                        Json::Value usage(Json::objectValue);
                        usage["prompt_tokens"]     = (Json::UInt64)prompt_toks;
                        usage["completion_tokens"] = (Json::UInt64)completion_toks;
                        usage["total_tokens"]      = (Json::UInt64)(prompt_toks + completion_toks);
                        usage_root["usage"] = usage;
                        Json::StreamWriterBuilder w;
                        w["indentation"] = "";
                        std::string uchunk = "data: " + Json::writeString(w, usage_root) + "\n\n";
                        sink.write(uchunk.data(), uchunk.size());

                        std::string done = "data: [DONE]\n\n";
                        sink.write(done.data(), done.size());
                        sink.done();
                        return false;
                    });
                } else {
                    // --- NON-STREAMING MODE ---
                    std::string response_json = generate_pipeline->generate_batch(prompts, options, generate_modelName);
                    res.set_content(response_json, "application/json");
                    res.status = 200;
                }

            } catch (const std::exception& e) {
                send_error(res, e.what(), 400);
            }
        };
        
        auto translate_handler = [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/translate request received." << std::endl;
            
            try {
                if (!translation_pipeline) {
                    throw std::invalid_argument("[Translate] pipeline not loaded. Pass a model with tokenizer.model via -m");
                }

                std::vector<std::string> texts;
                std::string src_lang;
                std::string tgt_lang;
                ctranslate2::TranslationOptions options;
                // Reasonable T5 defaults
                options.max_decoding_length = 128;
                options.beam_size = 4;
                bool is_stream = false;
                bool use_sampling = false;
                options.return_scores = true;

                parse_request_generate("input",
                                       req.body,
                                       texts,
                                       options,
                                       src_lang,
                                       tgt_lang,
                                       is_stream,
                                       use_sampling);
                
                if (texts.empty()) {
                    throw std::invalid_argument("'input' field must be a non-empty string or array of strings.");
                }

                if (is_stream) {
                    // --- STREAMING MODE ---
                    TranslationPipeline* raw_pipeline = translation_pipeline.get();
                    size_t num_texts = texts.size();

                    res.set_chunked_content_provider("text/event-stream",
                        [raw_pipeline, texts, src_lang, tgt_lang, options, num_texts, use_sampling](size_t /*offset*/, httplib::DataSink& sink) {

                        // Accumulate per-sentence buffers so we can send a final
                        // complete JSON object once all text has been streamed.
                        std::vector<std::string> full_texts(num_texts, "");

                        auto token_callback = [&](const std::string& token, int sentence_idx) -> bool {
                            if (sentence_idx < 0 || (size_t)sentence_idx >= num_texts) return true;
                            full_texts[sentence_idx] += token;

                            // Build an SSE chunk: one translation entry per sentence
                            Json::Value root(Json::objectValue);
                            root["object"] = "translation.chunk";
                            Json::Value translationsNode(Json::arrayValue);
                            Json::Value translationNode(Json::objectValue);
                            translationNode["index"] = sentence_idx;
                            translationNode["delta"] = token;
                            translationsNode.append(translationNode);
                            root["translations"] = translationsNode;

                            Json::StreamWriterBuilder writer;
                            writer["indentation"] = "";
                            std::string chunk = "data: " + Json::writeString(writer, root) + "\n\n";
                            sink.write(chunk.data(), chunk.size());

                            return true; // keep streaming
                        };

                        raw_pipeline->translate_batch_stream(texts, src_lang, tgt_lang, options, token_callback, use_sampling);

                        // Send [DONE] sentinel to signal end of stream
                        std::string done = "data: [DONE]\n\n";
                        sink.write(done.data(), done.size());
                        sink.done();
                        return false;
                    });
                } else {
                    // --- NON-STREAMING MODE (original behaviour) ---
                    std::string response_json = translation_pipeline->translate_batch(texts, src_lang, tgt_lang, options);
                    
                    res.set_content(response_json, "application/json");
                    res.status = 200;
                }
                
            } catch (const std::exception& e) {
                send_error(res, e.what(), 400);
            }
        };
        
        // Route: /v1/chat/completions  (smart dispatch based on request keys)
        // • "messages" → chat completion (LLM)
        // • "prompt"   → /v1/generate   (T5 encoder-decoder)
        // • "input"     → /v1/translate  (NMT seq2seq)
        svr.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/chat/completions request received." << std::endl;
            
            try {
                
                // --- Detect request type by key presence ---
                nlohmann::json peek;
                try { peek = nlohmann::json::parse(req.body); }
                catch (...) { throw std::invalid_argument("Malformed JSON body."); }

                const bool has_messages = peek.contains("messages")
                && peek["messages"].is_array() && peek["messages"].size() != 0;
                const bool has_prompt   = peek.contains("prompt");
                const bool has_text     = peek.contains("input");
                
                if (!has_messages && !has_prompt && has_text) {
                    translate_handler(req, res);
                }
                else if (!has_messages && has_prompt) {
                    generate_handler(req, res);
                }
                else {
                    chat_completions_handler(req, res);
                }
                
            } catch (const std::exception& e) {
                send_error(res, e.what(), 400);
            }
        });
        
        // Route: /v1/generate
        svr.Post("/v1/generate", generate_handler);
        
        // Route: /v1/translate
        svr.Post("/v1/translate", translate_handler);
                 
        // Route: /v1/rerank
        svr.Post("/v1/rerank", [&](const httplib::Request& req, httplib::Response& res) {
         
            std::cout << "[Server] /v1/rerank request received." << std::endl;
            
            try {
                
                if(reranking_model_created == 0) {
                    throw std::invalid_argument("[Rerank] Model not loaded.");
                }
                
                std::string query;
                int top_n = -1;
                std::vector<std::string> documents;
                before_run_reranking(req.body, query, &top_n, documents);
                
                std::string response_json;
                
                response_json = rerank_pipeline->rerank_batch(query, top_n, documents);
                res.set_content(response_json, "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                send_error(res, e.what(), 400);
            }
        });
        
        // Route: /v1/embeddings
        svr.Post("/v1/embeddings", [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/embeddings request received." << std::endl;
            
            try {
                if(embedding_model_created == 0) {
                    throw std::invalid_argument("[Embedding] Model not loaded.");
                }
                std::vector<std::string> texts;
                before_run_embeddings(req.body, texts);
                std::string response_json = pipeline->embed_batch(texts, pooling_mode);
                res.set_content(response_json, "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                send_error(res, e.what(), 400);
            }
        });
        
        // Route: /v1/contextualizedembeddings
        auto contextualized_embeddings_handler = [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/contextualizedembeddings request received." << std::endl;
            
            try {
                
                if(embedding_model_created == 0) {
                    throw std::invalid_argument("[Embedding] Model not loaded.");
                }
                
                std::vector<std::string> texts;
                
                // Parse and concatenate context + chunks
                before_run_contextualized_embeddings(req.body, texts);
                
                // Compute embeddings
                std::string response_json = pipeline->embed_batch(texts, pooling_mode);
                
                // Return response
                res.set_content(response_json, "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                send_error(res, e.what(), 400);
            }
        };
        
        svr.Post("/v1/contextualizedembeddings", contextualized_embeddings_handler);
        svr.Post("/v1/contextualized/embeddings", contextualized_embeddings_handler);
        
        std::cout << "[Server] Listening on " << host << ":" << port << std::endl;
        
        svr.new_task_queue = []{ return new httplib::ThreadPool(2); };
        // Listen (Blocking call)
        if (!svr.listen(host.c_str(), port)) {
            std::cerr << "Error: Could not start server on " << host << ":" << port << std::endl;
            return 1;
        }
    }
    // ---------------------------------------------------------
    // CLI MODE
    // ---------------------------------------------------------
    else {
        // Handle input file reading if not piped via stdin ('-')
        if ((!cli_request_json.size()) && (input_path != NULL)) {
            FILE *f = _fopen(input_path, _rb);
            if(f) {
                fseek(f, 0, SEEK_END);
                size_t len = (size_t)ftell(f);
                fseek(f, 0, SEEK_SET);
                cli_request_json.resize(len);
                fread(cli_request_json.data(), 1, cli_request_json.size(), f);
                fclose(f);
            }
        }
        
        if (cli_request_json.size() == 0) {
            usage();
            return 1;
        }
        
        std::string request_str((const char *)cli_request_json.data(), cli_request_json.size());
        std::string response;
        std::vector<std::string> texts;
        
        try {
            if(pipeline != nullptr) {
                before_run_embeddings(request_str, texts);
                response = pipeline->embed_batch(texts, pooling_mode);
            }
        } catch (const std::exception& e) {
            // CLI Error Format
            Json::Value rootNode(Json::objectValue);
            Json::Value errorNode(Json::objectValue);
            rootNode["error"] = errorNode;
            errorNode["message"] = e.what();
            errorNode["type"] = "invalid_request_error";
            
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            response = Json::writeString(writer, rootNode);
        }
        
        // Output logic
        if(!output_path) {
            std::cout << response << std::endl;
        } else {
            FILE *f = _fopen(output_path, _wb);
            if(f) {
                fwrite(response.c_str(), 1, response.length(), f);
                fclose(f);
            }
        }
    }
    
    return 0;
}
