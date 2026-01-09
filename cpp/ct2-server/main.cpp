//
//  main.cpp
//  ct2-server
//
//  Created by miyako on 2026/01/09.
//

#include "ct2-server.h"

// Enum for selecting the pooling strategy
enum class PoolingStrategy {
    CLS,        // Use the first token (usually [CLS])
    LAST_TOKEN, // Use the last token
    MEAN        // Average of all tokens
};

namespace fs = std::filesystem;

class EmbeddingPipeline {
public:
    // OPTIMIZATION TIP 1: intra_threads
    // Set intra_threads to the number of physical cores you want to use for *one* batch.
    // If you are processing one batch at a time, set this to total cores (e.g., 4 or 8).
    EmbeddingPipeline(const std::string& model_dir,
                      int intra_threads = 4,
                      const std::string& device = "cpu") {
        
        // Load Tokenizer
        fs::path json_path = fs::path(model_dir) / "tokenizer.json";
        std::ifstream file(json_path.string());
        if (!file.is_open()) throw std::runtime_error("No tokenizer.json found");
        std::stringstream buffer;
        buffer << file.rdbuf();
        
        tokenizer_ = tokenizers::Tokenizer::FromBlobJSON(buffer.str());

        // Setup Device
        ctranslate2::Device device_type = (device == "cuda") ?
            ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
        std::vector<int> device_indices = {0};

        // OPTIMIZATION TIP 2: Compute Type
        // Use INT8 if your CPU supports AVX2/AVX512.
        // Even if the model file is float32, this forces int8 computation.
        ctranslate2::ComputeType compute_type = ctranslate2::ComputeType::INT8;

        encoder_ = std::make_unique<ctranslate2::Encoder>(
            model_dir,
            device_type//,
//            device_indices,
//            compute_type,
//            0, // max_queued_batches
//            1, // inter_threads (parallel batches - keep 1 if doing big batches)
//            intra_threads // intra_threads (parallel ops within batch)
        );
    }

    // Helper to tokenize single string
    std::vector<int32_t> tokenize_one(const std::string& text) {
        return tokenizer_->Encode(text);
    }

    // OPTIMIZATION TIP 3: Batch Processing
    std::vector<std::vector<float>> embed_batch(const std::vector<std::string>& texts,
                                                PoolingStrategy strategy,
                                                bool l2_normalize = true) {
        if (texts.empty()) return {};

        // 1. Tokenize Batch (Sequential on CPU, but fast)
        std::vector<std::vector<size_t>> batch_ids;
        std::vector<size_t> lengths;
        batch_ids.reserve(texts.size());
        lengths.reserve(texts.size());

        for (const auto& text : texts) {
            auto ids_int = tokenize_one(text);
            
            // Convert int32 to size_t
            std::vector<size_t> ids_size_t;
            ids_size_t.reserve(ids_int.size());
            for (auto id : ids_int) ids_size_t.push_back(static_cast<size_t>(id));
            
            lengths.push_back(ids_size_t.size());
            batch_ids.push_back(std::move(ids_size_t));
        }

        // 2. Forward Pass
        // CTranslate2 automatically handles padding for the batch
        auto future = encoder_->forward_batch_async(batch_ids);
        ctranslate2::EncoderForwardOutput result = future.get();

        // 3. Extract Data
        // shape: [batch_size, max_seq_len, hidden_dim]
        const auto& hidden_states = result.last_hidden_state;
        const float* data = hidden_states.data<float>();
        const auto& shape = hidden_states.shape();
        
        size_t batch_size = shape[0];
        size_t max_seq_len = shape[1];
        size_t hidden_dim = shape[2];
        size_t stride_batch = max_seq_len * hidden_dim;

        std::vector<std::vector<float>> output_embeddings;
        output_embeddings.reserve(batch_size);

        // 4. Pool per item in batch
        for (size_t b = 0; b < batch_size; ++b) {
            std::vector<float> embedding(hidden_dim, 0.0f);
            size_t current_len = lengths[b]; // The actual non-padded length
            
            // Pointer to start of this sentence in the flattened array
            const float* sentence_data = data + (b * stride_batch);

            if (current_len == 0) {
                // Handle empty string edge case
                output_embeddings.push_back(embedding);
                continue;
            }

            if (strategy == PoolingStrategy::CLS) {
                // First token (index 0)
                for (size_t i = 0; i < hidden_dim; ++i) {
                    embedding[i] = sentence_data[i];
                }
            }
            else if (strategy == PoolingStrategy::LAST_TOKEN) {
                // Last REAL token (not the padded end)
                size_t offset = (current_len - 1) * hidden_dim;
                for (size_t i = 0; i < hidden_dim; ++i) {
                    embedding[i] = sentence_data[offset + i];
                }
            }
            else if (strategy == PoolingStrategy::MEAN) {
                // Sum actual tokens only
                for (size_t t = 0; t < current_len; ++t) {
                    size_t offset = t * hidden_dim;
                    for (size_t i = 0; i < hidden_dim; ++i) {
                        embedding[i] += sentence_data[offset + i];
                    }
                }
                // Average by ACTUAL length
                float div = static_cast<float>(current_len);
                for (size_t i = 0; i < hidden_dim; ++i) {
                    embedding[i] /= div;
                }
            }

            if (l2_normalize) {
                float sum_sq = 0.0f;
                for (float val : embedding) sum_sq += val * val;
                float norm = std::sqrt(sum_sq);
                if (norm > 1e-9) {
                    for (float& val : embedding) val /= norm;
                }
            }

            output_embeddings.push_back(std::move(embedding));
        }

        return output_embeddings;
    }

private:
    std::unique_ptr<ctranslate2::Encoder> encoder_;
    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;
};

namespace fs = std::filesystem;
using namespace tokenizers; // mlc-ai namespace

static // Helper: Read entire file into a string (Blob)
std::string LoadBytesFromFile(const std::string& path) {
    std::ifstream fs(path, std::ios::in | std::ios::binary);
    if (!fs) throw std::runtime_error("Could not open file: " + path);
    
    std::string data((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    return data;
}

static // Unified Loader
std::unique_ptr<Tokenizer> LoadTokenizer(const std::string& model_path) {
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
        return Tokenizer::FromBlobJSON(blob);
    }
    
    // 3. Fallback to SentencePiece
    if (fs::exists(model_file_path) && model_file_path.extension() == ".model") {
        std::cout << "Loading SentencePiece from: " << model_file_path << std::endl;
        std::string blob = LoadBytesFromFile(model_file_path.string());
        return Tokenizer::FromBlobSentencePiece(blob);
    }

    return 0;
}

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

// Improved signature: uses Eigen::Ref to avoid copies if passing blocks/maps
Eigen::VectorXf mean_pool(
    const Eigen::Ref<const Eigen::MatrixXf>& hidden,
    const Eigen::Ref<const Eigen::VectorXi>& mask
) {
    // 1. Safety Check
    if (hidden.rows() != mask.size()) {
        throw std::invalid_argument("Hidden state sequence length does not match mask length.");
    }

    // 2. Convert mask to float for matrix multiplication
    // Casting is usually very fast compared to the accumulation logic
    Eigen::VectorXf mask_f = mask.cast<float>();

    // 3. Calculate Count (Sum of mask)
    float count = mask_f.sum();
    
    // Edge case: empty mask
    if (count <= 0.0f) {
        return Eigen::VectorXf::Zero(hidden.cols());
    }

    // 4. Matrix Multiplication approach (The main optimization)
    // Formula: (1/N) * (mask^T * Hidden)
    //
    // mask_f             is [seq_len, 1]
    // hidden             is [seq_len, hidden_dim]
    // mask_f.transpose() is [1, seq_len]
    // result             is [1, hidden_dim]
    
    // Note: We create a temporary row vector, then transpose it back
    // to match the return type (VectorXf is a column vector).
    Eigen::VectorXf pooled = (mask_f.transpose() * hidden).transpose();

    return pooled / count;
}

Eigen::MatrixXf mean_pool_batch(
    const std::vector<Eigen::MatrixXf>& hidden_batch,
    const std::vector<Eigen::VectorXi>& mask_batch
) {
    // 1. Safety Checks
    if (hidden_batch.empty()) {
        return Eigen::MatrixXf(0, 0);
    }
    if (hidden_batch.size() != mask_batch.size()) {
        throw std::invalid_argument("Batch size mismatch between hidden states and masks.");
    }

    long batch_size = hidden_batch.size();
    long hidden_dim = hidden_batch[0].cols();

    // Allocate the result matrix once
    Eigen::MatrixXf out(batch_size, hidden_dim);

    // 2. Parallel Processing (OpenMP)
    // This distributes the rows across available CPU cores.
    #pragma omp parallel for
    for (int i = 0; i < batch_size; ++i) {
        
        // --- Step A: Optimized Mean Pooling (Inlined) ---
        // We write directly into out.row(i) to avoid creating temporary VectorXf objects.
        
        const auto& hidden = hidden_batch[i];
        const auto& mask = mask_batch[i];

        // Convert mask to float for calculation
        Eigen::VectorXf mask_f = mask.cast<float>();
        float count = mask_f.sum();

        if (count > 0.0f) {
            // Matrix Mult: [1, seq] * [seq, dim] -> [1, dim]
            // We assign this directly to the output row.
            out.row(i) = mask_f.transpose() * hidden;
            out.row(i) /= count;
            
            // --- Step B: Optimized L2 Normalize (In-Place) ---
            // Calculate norm of the row we just wrote
            float norm = out.row(i).norm();
            
            if (norm > 1e-12f) {
                out.row(i) /= norm;
            }
        } else {
            // Handle edge case: empty mask -> zero vector
            out.row(i).setZero();
        }
    }

    return out;
}

Eigen::VectorXf l2_normalize(const Eigen::Ref<const Eigen::VectorXf>& v) {
    float norm = v.norm();
    // Use a small epsilon to prevent division by near-zero values
    // and ensure numerical stability.
    if (norm > 1e-12f)
        return v.normalized(); // Uses Eigen's optimized internal implementation
    // If norm is effectively zero, return the original (zero) vector
    return v;
}

#pragma mark -

static void usage(void)
{
    fprintf(stderr, "Usage:  ct2-server -e model -i input\n\n");
    fprintf(stderr, "onnx-genai\n\n");
    fprintf(stderr, " -%c path     : %s\n", 'm' , "model");
    fprintf(stderr, " -%c path     : %s\n", 'e' , "embedding model");
    fprintf(stderr, " -%c          : %s\n", 'j' , "chat template from stdin");
    fprintf(stderr, " -%c path     : %s\n", 't' , "chat template");
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
#define ARGS (OPTARG_T)L"m:e:i:o:sp:jt:bcld-h"
#define _atoi _wtoi
#define _atof _wtof
#else
#define ARGS "m:e:i:o:sp:jt:bcld-h"
#define _atoi atoi
#define _atof atof
#endif

#pragma mark -

static long long get_created_timestamp() {
    // std::time(nullptr) returns the current time as a time_t (seconds since epoch)
    return static_cast<long long>(std::time(nullptr));
}

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

// Generate a fingerprint based on model identity and hardware
static std::string get_system_fingerprint(const std::string& model_path, const std::string& provider) {
    // 1. Combine identifying factors (Model + Engine)
    std::string identifier = model_path + "_" + provider;
    
    // 2. Hash the string to get a unique number
    std::hash<std::string> hasher;
    size_t hash = hasher(identifier);
    
    // 3. Format as hex (e.g., "fp_1a2b3c4d")
    std::stringstream ss;
    ss << "fp_" << std::hex << hash;
    
    return ss.str();
}

static std::string get_openai_style_id() {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    
    std::string id = "chatcmpl-";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, max_index - 1);
    
    for (int i = 0; i < 29; ++i) {
        id += charset[dis(gen)];
    }
    return id;
}

#pragma mark -

static void parse_request_embeddings(const std::string &json,
                                     std::string &input) {
    
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    Json::CharReader *reader = builder.newCharReader();
    bool parse = reader->parse(json.c_str(),
                               json.c_str() + json.size(),
                               &root,
                               &errors);
    delete reader;
    
    if(parse)
    {
        if(root.isObject())
        {
            Json::Value input_node = root["input"];
            if(input_node.isString())
            {
                input = input_node.asString();
            }
        }
    }
}

static void parse_request(
                          const std::string &json,
                          std::string &prompt,
                          unsigned int *max_tokens,
                          unsigned int *top_k,
                          double *top_p,
                          double *temperature,
                          double *repetition_penalty,
                          unsigned int *n,
                          bool *is_stream,
                          void* tokenizer,
                          std::string& chat_template) {
    
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    
    Json::CharReader *reader = builder.newCharReader();
    bool parse = reader->parse(json.c_str(),
                               json.c_str() + json.size(),
                               &root,
                               &errors);
    delete reader;
        
    if(parse)
    {
        if(root.isObject())
        {
            Json::Value messages_node = root["messages"];
            if(messages_node.isArray())
            {
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                std::string messages_json = Json::writeString(writer, messages_node);

            }
            Json::Value top_p_node = root["top_p"];
            if(top_p_node.isNumeric())
            {
                *top_p = top_p_node.asDouble();
            }
            Json::Value top_k_node = root["top_k"];
            if(top_k_node.isNumeric())
            {
                *top_k = top_k_node.asInt();
            }
            Json::Value max_tokens_node = root["max_tokens"];
            if(max_tokens_node.isNumeric())
            {
                *max_tokens = max_tokens_node.asInt();
            }
            Json::Value repetition_penalty_node = root["repetition_penalty"];
            if(repetition_penalty_node.isNumeric())
            {
                *repetition_penalty = repetition_penalty_node.asDouble();
            }
            /*
             only these are set by AI-Kit
             */
            Json::Value temperature_node = root["temperature"];
            if(temperature_node.isNumeric())
            {
                *temperature = temperature_node.asDouble();
            }
            Json::Value n_node = root["n"];
            if(n_node.isNumeric())
            {
                *n = n_node.asInt();
            }
            max_tokens_node = root["max_completion_tokens"];
            if(max_tokens_node.isNumeric())
            {
                *max_tokens = max_tokens_node.asInt();
            }
            Json::Value stream_node = root["stream"];
            if(stream_node.isBool())
            {
                *is_stream = stream_node.asBool();
            }
        }
    }
}

static void before_run_embeddings(
                                  const std::string& request_body,
                                  std::string &input
                                  ) {
    parse_request_embeddings(request_body, input);
}

static void before_run_inference(
                                 const std::string& request_body,
                                 std::string &prompt,
                                 unsigned int *max_tokens,
                                 unsigned int *top_k,
                                 double *top_p,
                                 double *temperature,
                                 double *repetition_penalty,
                                 unsigned int *n,
                                 bool *is_stream,
                                 void* tokenizer,
                                 std::string& chat_template) {
    
    parse_request(request_body, prompt, max_tokens, top_k, top_p, temperature, repetition_penalty, n, is_stream, tokenizer, chat_template);
}

static std::string run_inference(
                                 void* model,
                                 void* tokenizer,
                                 const std::string& modelName,
                                 const std::string& fingerprint,
                                 long long created,
                                 unsigned int max_tokens,
                                 unsigned int top_k,
                                 double top_p,
                                 double temperature,
                                 double repetition_penalty,
                                 unsigned int n,
                                 std::string prompt
                                 ) {
    /*
     The chat completion object
     https://platform.openai.com/docs/api-reference/chat/object
     */
    std::string content;
    Json::Value rootNode(Json::objectValue);
    size_t completion_tokens = 0;
    size_t input_token_count = 0;
    std::string finish_reason = "stop";//length, content_filter, tool_calls, function_call
    
    try {
        // Create Tokenizer Stream
        
        // Encode Prompt
        
        // Set Generation Parameters
          
        // Define your multiple stop conditions
        
        // Create Generator
        // Generator is stateful; we need 1 per request.
                
        // Create a vector of streams
        // Decoding is stateful; we need 1 decoder per sequence.
        std::vector<std::string> generated_responses(n);
        
        // Start Generating
        
        // Build Response JSON
        rootNode["id"] = get_openai_style_id();
        rootNode["object"] = "chat.completion";
        rootNode["created"] = created;
        rootNode["model"] = modelName;
        rootNode["system_fingerprint"] = fingerprint;//Deprecated
        rootNode["service_tier"] = "default";
        Json::Value choicesNode(Json::arrayValue);
        
        for (int i = 0; i < n; i++) {
            Json::Value choiceNode(Json::objectValue);
            choiceNode["index"] = i;
            Json::Value messageNode(Json::objectValue);
            messageNode["role"] = "assistant";
            messageNode["content"] = generated_responses[i].c_str();
            messageNode["refusal"] = Json::nullValue;
            choiceNode["message"] = messageNode;
            choicesNode.append(choiceNode);
            choiceNode["logprobs"] = Json::nullValue;
            choiceNode["finish_reason"] = finish_reason;
        }
        rootNode["choices"] = choicesNode;
        
        Json::Value usageNode(Json::objectValue);
        usageNode["prompt_tokens"] = (Json::Int)input_token_count;
        usageNode["completion_tokens"] = (Json::Int)completion_tokens;
        usageNode["total_tokens"] = 0;
        
        Json::Value promptTokenDetailsNode(Json::objectValue);
        promptTokenDetailsNode["cached_tokens"] = 0;
        promptTokenDetailsNode["audio_tokens"] = 0;
        usageNode["prompt_tokens_details"] = promptTokenDetailsNode;
        
        Json::Value completionTokenDetailsNode(Json::objectValue);
        completionTokenDetailsNode["reasoning_tokens"] = 0;
        completionTokenDetailsNode["audio_tokens"] = 0;
        completionTokenDetailsNode["accepted_prediction_tokens"] = 0;
        completionTokenDetailsNode["rejected_prediction_tokens"] = 0;
        usageNode["completion_tokens_details"] = completionTokenDetailsNode;
        
        rootNode["usage"] = usageNode;
        
    } catch (const std::exception& e) {
        throw;
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, rootNode);
}

/*
 The chat completion chunk object
 https://platform.openai.com/docs/api-reference/chat-streaming/streaming
 */
static std::string create_stream_chunk(int n,
                                       const std::string& id,
                                       const std::string& model,
                                       const std::string& fingerprint,
                                       const std::string& content,
                                       bool finish) {
    Json::Value root;
    root["id"] = id;
    root["object"] = "chat.completion.chunk";
    root["created"] = (Json::UInt64)std::time(nullptr);
    root["model"] = model;
    root["system_fingerprint"] = fingerprint;//Deprecated
    
    Json::Value choice;
    choice["index"] = n;
    
    Json::Value delta;
    if (content.empty() && !finish) {
        delta["role"] = "assistant";
    } else {
        delta["content"] = content;
    }
    delta["logprobs"] = Json::nullValue;
    choice["delta"] = delta;
    
    if (finish) {
        choice["finish_reason"] = "stop";
    } else {
        choice["finish_reason"] = Json::nullValue;
    }
    root["choices"].append(choice);
    
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return "data: " + Json::writeString(writer, root) + "\n\n";
}

static void run_inference_stream(
                                 void* model,
                                 void* tokenizer,
                                 const std::string& modelName,
                                 const std::string& fingerprint,
                                 long long created,
                                 unsigned int max_tokens,
                                 unsigned int top_k,
                                 double top_p,
                                 double temperature,
                                 double repetition_penalty,
                                 unsigned int n,
                                 std::string prompt,
                                 std::function<bool(const std::string&, unsigned int)> on_token_generated
                                 ) {
    
    // Create Tokenizer Stream
    
    size_t input_token_count = 0;
    double max_length = 0;
    
    // Encode Prompt
    
    // Set Generation Parameters
     
    // Define your multiple stop conditions
    
    // Create Generator
    // Generator is stateful; we need 1 per request.
    
    // Create a vector of streams
    // Decoding is stateful; we need 1 decoder per sequence.
    std::vector<std::string> generated_responses(n);
    
    // Start Generating
}

static // Helper to convert int32 -> int64
std::vector<int64_t> ConvertToInt64(const std::vector<int>& input_ids) {
    std::vector<int64_t> output(input_ids.size());
    std::transform(input_ids.begin(), input_ids.end(), output.begin(),
                   [](int i) { return static_cast<int64_t>(i); });
    return output;
}

static std::string run_embeddings(
                                  void *session,
                                  std::vector<int>& ids,
                                std::vector<const char*>&  input_names_c_array,
                                  size_t num_input_nodes,
                                  std::vector<const char*>&   output_names_c_array,
                                  size_t num_output_nodes,
                                  PoolingMode pooling_mode) {

    int batch_size = 1;
    std::vector<int64_t> input_ids = ConvertToInt64(ids);
    int seq_len = (int)ids.size();
    
    std::string reponseJson;
    
    try {
        
        std::vector<int64_t> input_node_dims = {batch_size, (int64_t)input_ids.size()};

        // Define Shapes
        std::vector<int64_t> input_dims = {batch_size, seq_len};
        // Create Attention Mask (1 for real tokens)
        std::vector<int64_t> attention_mask(seq_len, 1);
        // Create the Missing Vector (All Zeros)
        std::vector<int64_t> token_type_ids(seq_len, 0);
        // Create Inputs Vector
        
        // Mistral / Llama / Qwen: only need input_ids
        
        switch (pooling_mode) {
            case POOLING_COLBERT:

                break;
            case POOLING_CLS:

                break;
            case POOLING_LAST_TOKEN:

                break;
            case POOLING_MEAN:
            default:

                break;
        }

    } catch (const std::exception& e) {
        throw;
    }

    return reponseJson;
}

static std::string run_embeddings_e2e(
                                  void *session,
                                  std::string& input,
                                  std::vector<const char*>&  input_names_c_array,
                                  size_t num_input_nodes,
                                  std::vector<const char*>&   output_names_c_array,
                                  size_t num_output_nodes) {

    Json::Value rootNode(Json::objectValue);
    try {
        const char* input_strings[] = { input.c_str() };
        size_t batch_size = 1;
        int64_t input_shape[] = { (int64_t)batch_size };
        
    } catch (const std::exception& e) {
        throw;
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, rootNode);
}

#pragma mark -

int main(int argc, OPTARG_T argv[]) {
    
#ifdef WIN32
    std::wstring model_path_u16;
    std::wstring embedding_model_path_u16;
#endif
    std::string model_path;           // -m
    std::string embedding_model_path; // -e
    std::string chat_template;        // -j
    OPTARG_T input_path  = NULL;      // -i
    OPTARG_T output_path = NULL;      // -o
    OPTARG_T chat_template_path = NULL;
        
    PoolingMode pooling_mode = POOLING_MEAN;
    
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
                pooling_mode = POOLING_COLBERT;
                break;
            case 'c':
                pooling_mode = POOLING_CLS;
                break;
            case 'l':
                pooling_mode = POOLING_LAST_TOKEN;
                break;
            case 'd':
                pooling_mode = POOLING_E2E;
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
    
    std::string fingerprint;
    long long model_created = 0;
    std::string modelName;
    
    if (model_path.length() != 0) {
        if (fs::exists(model_path)) {
            if (fs::is_directory(model_path)) {
                // 1.a Initialize Model and Tokenizer (Load once)
                std::cerr << "[Chat] Loading from " << model_path << std::endl;
                fingerprint = get_system_fingerprint(model_path, "directml");
                modelName = get_model_name(model_path);
                try {
//==========================================================
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
    std::vector<std::string> input_node_names;
    std::vector<std::string> output_node_names;
    std::vector<int64_t> input_shape = {1}; // Batch size 1
    std::vector<const char*> input_names_c_array;
    std::vector<const char*> output_names_c_array;

    std::unique_ptr<EmbeddingPipeline> pipeline;
    
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
                    fs::path tokenizer_path = fs::path(embedding_model_path) / "tokenizer.json";
                    pipeline = std::make_unique<EmbeddingPipeline>(
                                                                   embedding_model_path,
                                                                   4,
                                                                   "cpu"
                                                                   );
                    embedding_model_created = get_created_timestamp();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to load model: " << e.what() << std::endl;
                    return 1;
                }
            }
        }
    }
    
    // ---------------------------------------------------------
    // SERVER MODE
    // ---------------------------------------------------------
    if (server_mode) {
        httplib::Server svr;
        
        // Route: /v1/chat/completions
        svr.Post("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/chat/completions request received." << std::endl;
            
            try {
                
                if(model_created == 0) {
                    throw std::invalid_argument("[Chat] Model not loaded.");
                }
                
                std::string prompt;
                unsigned int max_tokens = 2048;
                unsigned int top_k = 50;
                double top_p = 0.9;
                double temperature = 0.7;
                double repetition_penalty = 1.2;
                unsigned int n = 1;
                bool is_stream = false;
                
                if(is_stream) {
                    std::string req_id = get_openai_style_id();
                    
                    // Corrected Lambda structure
                    res.set_chunked_content_provider("text/event-stream",
                                                     [&, req_id, prompt, max_tokens, top_k, top_p, temperature, n ](size_t offset, httplib::DataSink &sink) {
                        // Send initial role packet (optional but good practice)
                        for (int i = 0; i < n; i++) {
                            std::string role_chunk = create_stream_chunk(i, req_id, modelName, fingerprint, "");
                            sink.write(role_chunk.data(), role_chunk.size());
                        }
                        
                        // Define a callback to handle tokens as they are generated
                        auto token_callback = [&](const std::string& token, unsigned int n) {
                            std::string chunk = create_stream_chunk(n, req_id, modelName, fingerprint, token);
                            sink.write(chunk.data(), chunk.size());
                            return true; // Return false to stop inference if needed
                        };
                        
                        // Run Inference (You must implement run_inference_stream)
                        // Note: This function must block here until finished, calling token_callback repeatedly
                        
                        // 4. Send finish reason
                        std::string finish_chunk = create_stream_chunk(n, req_id, modelName, fingerprint, "", true);
                        sink.write(finish_chunk.data(), finish_chunk.size());
                        
                        // 5. Send [DONE] to close the stream for the client
                        std::string done = "data: [DONE]\n\n";
                        sink.write(done.data(), done.size());
                        
                        sink.done(); // Close the connection
                        return true;
                    }
                                                     );
                    
                }else{
                    // Run Inference
                    std::string response_json = "{}";
                    res.set_content(response_json, "application/json");
                    res.status = 200;
                }
            } catch (const std::exception& e) {
                // Build Error JSON
                Json::Value rootNode(Json::objectValue);
                Json::Value errorNode(Json::objectValue);
                errorNode["message"] = e.what();
                errorNode["type"] = "invalid_request_error";
                errorNode["param"] = Json::nullValue;
                errorNode["code"] = Json::nullValue;
                rootNode["error"] = errorNode;
                
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                std::string error_str = Json::writeString(writer, rootNode);
                
                res.set_content(error_str, "application/json");
                res.status = 400; // Bad Request as per requirement
                std::cerr << "[Server] Error: " << e.what() << std::endl;
            }
        });
        
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
            // Serialize
            Json::StreamWriterBuilder writer;
            writer["indentation"] = ""; // Minified JSON
            std::string json_str = Json::writeString(writer, root);
            // Respond
            res.set_content(json_str, "application/json");
            res.status = 200;
        });
        
        // Route: /v1/embeddings
        svr.Post("/v1/embeddings", [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/embeddings request received." << std::endl;
            
            try {
                if(embedding_model_created == 0) {
                    throw std::invalid_argument("[Embedding] Model not loaded.");
                }
                std::string text;
                before_run_embeddings(req.body, text);
                auto embeddings = pipeline->embed_batch({text}, PoolingStrategy::MEAN, true);
                Json::Value rootNode(Json::objectValue);
                Json::Value embeddingsNode(Json::arrayValue);
                for (float val : embeddings[0]) {
                    embeddingsNode.append(val);
                }
                rootNode["embedding"] = embeddingsNode;
                rootNode["index"] = 0;
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                std::string response_json = Json::writeString(writer, rootNode);
                res.set_content(response_json, "application/json");
                res.status = 200;
            } catch (const std::exception& e) {
                // Build Error JSON
                Json::Value rootNode(Json::objectValue);
                Json::Value errorNode(Json::objectValue);
                errorNode["message"] = e.what();
                errorNode["type"] = "invalid_request_error";
                errorNode["param"] = Json::nullValue;
                errorNode["code"] = Json::nullValue;
                rootNode["error"] = errorNode;
                
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                std::string error_str = Json::writeString(writer, rootNode);
                
                res.set_content(error_str, "application/json");
                res.status = 400; // Bad Request as per requirement
                std::cerr << "[Server] Error: " << e.what() << std::endl;
            }
            
        });
        
        std::cout << "[Server] Listening on " << host << ":" << port << std::endl;
        
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
        std::string text;
        
        try {
            before_run_embeddings(request_str, text);
            auto embeddings = pipeline->embed_batch({text}, PoolingStrategy::MEAN, true);
            Json::Value rootNode(Json::objectValue);
            Json::Value embeddingsNode(Json::arrayValue);
            for (float val : embeddings[0]) {
                embeddingsNode.append(val);
            }
            rootNode["embedding"] = embeddingsNode;
            rootNode["index"] = 0;
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            response = Json::writeString(writer, rootNode);
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
