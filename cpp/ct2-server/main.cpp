//
//  main.cpp
//  ct2-server
//
//  Created by miyako on 2026/01/09.
//

#include "ct2-server.h"

namespace fs = std::filesystem;
using namespace tokenizers;

static // Helper: Read entire file into a string (Blob)
std::string LoadBytesFromFile(const std::string& path) {
    std::ifstream fs(path, std::ios::in | std::ios::binary);
    if (!fs) throw std::runtime_error("Could not open file: " + path);
    
    std::string data((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    return data;
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

    return 0;
}

class TranslationService {
    public:
    TranslationService(const std::string& model_dir,
                       const std::string& source_sp_path,
                       int num_threads = 4,
                       const std::string& device = "cpu") {
        
        // --- 1. Optimize Threading ---
        // This sets the number of threads used for matrix multiplication (intra-op).
        // If you set this to 4, it uses 4 cores for the math.
        if (num_threads > 0) {
            ctranslate2::set_num_threads(num_threads);
        }
        // --- 2. Load Tokenizer ---
        fs::path sp_model_path = source_sp_path.length() == 0 ? fs::path(model_dir) / "tokenizer.model" : fs::path(source_sp_path);
        
        tokenizer_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
#if WIN32
const auto status = tokenizer_->Load(utf8_to_wstring(sp_model_path.c_str()));
#else
const auto status = tokenizer_->Load(sp_model_path.c_str());
#endif

        if (!status.ok()) {
            throw std::runtime_error("Failed to load SentencePiece model: " + status.ToString());
        }
        
        ctranslate2::Device device_type = (device == "cuda") ?
        ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
        translator_ = std::make_unique<ctranslate2::Translator>(
                                                                model_dir,
                                                                device_type
        );
    }

    std::string translate_batch(const std::vector<std::string>& texts,
                                const ctranslate2::TranslationOptions& options) {
        
        std::vector<std::vector<std::string>> batch_tokens;
        for (const auto& text : texts) {
            std::vector<std::string> tokens;
            tokenizer_->Encode(text, &tokens);
            batch_tokens.push_back(tokens);
        }

        auto results = translator_->translate_batch(batch_tokens, options);
        
        Json::Value rootNode(Json::objectValue);
        Json::Value translationsNode(Json::arrayValue);
        
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            Json::Value translationNode(Json::objectValue);
            translationNode["score"] = result.scores[0];
            Json::Value hypothesesNode(Json::arrayValue);
            for (const auto& hyp : result.hypotheses) {
                std::string detokenized;
                tokenizer_->Decode(hyp, &detokenized);
                hypothesesNode.append(detokenized);
            }
            translationNode["tokens"] = hypothesesNode;
            translationsNode.append(translationNode);
        }

        rootNode["translations"] = translationsNode;
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";

        return Json::writeString(writer, rootNode);;
    }
private:
    std::unique_ptr<ctranslate2::Translator> translator_;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> tokenizer_;
};

class EmbeddingPipeline {
public:
    // OPTIMIZATION TIP 1: intra_threads
    // Set intra_threads to the number of physical cores you want to use for *one* batch.
    // If you are processing one batch at a time, set this to total cores (e.g., 4 or 8).
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
        fs::path json_path = fs::path(model_dir) / "tokenizer.json";
        std::ifstream file(json_path.string());
        if (!file.is_open()) throw std::runtime_error("No tokenizer.json found");
        std::stringstream buffer;
        buffer << file.rdbuf();
        tokenizer_ = LoadTokenizer(model_dir);

        ctranslate2::Device device_type = (device == "cuda") ?
        ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
        encoder_ = std::make_unique<ctranslate2::Encoder>(model_dir, device_type);
    }

    // Helper to tokenize single string
    std::vector<int32_t> tokenize_one(const std::string& text) {
        return tokenizer_->Encode(text);
    }

    // OPTIMIZATION TIP 3: Batch Processing
    // Returns a matrix where each Row is an embedding vector
    std::string embed_batch(const std::vector<std::string>& texts,
                                                PoolingStrategy strategy,
                                                bool l2_normalize = true) {
        if (texts.empty()) return {};
        
        // 1. Tokenize & Prepare Batch
        std::vector<std::vector<size_t>> batch_ids;
        std::vector<size_t> lengths;
        batch_ids.reserve(texts.size());
        lengths.reserve(texts.size());
        
        for (const auto& text : texts) {
            auto ids_int = tokenize_one(text);
            std::vector<size_t> ids_size_t;
            ids_size_t.reserve(ids_int.size());
            for (auto id : ids_int) ids_size_t.push_back(static_cast<size_t>(id));
            lengths.push_back(ids_size_t.size());
            batch_ids.push_back(std::move(ids_size_t));
        }
        
        // 2. Forward Pass (Heavy Compute)
        auto future = encoder_->forward_batch_async(batch_ids);
        ctranslate2::EncoderForwardOutput result = future.get();
        
        // 3. Move to CPU (if needed)
        ctranslate2::StorageView hidden_states_cpu = result.last_hidden_state.to(ctranslate2::Device::CPU);
        
        // 4. Zero-Copy Eigen Mapping
        // CT2 Memory Layout: RowMajor [Batch, Time, Dim]
        float* raw_data = hidden_states_cpu.data<float>();
        const auto& shape = hidden_states_cpu.shape();
        
        long batch_size = shape[0];
        long max_seq_len = shape[1];
        long hidden_dim = shape[2];
        long stride_batch = max_seq_len * hidden_dim;
        
        std::vector<std::vector<float>> output_embeddings(batch_size);
        
        // 5. Compute Pooling per sentence using Eigen
        for (long b = 0; b < batch_size; ++b) {
            long valid_len = lengths[b];
            
            // Allocate space for the result vector
            output_embeddings[b].resize(hidden_dim);
            
            if (valid_len == 0) {
                std::fill(output_embeddings[b].begin(), output_embeddings[b].end(), 0.0f);
                continue;
            }
            
            // Map the output vector so Eigen can write directly to std::vector memory
            Eigen::Map<Eigen::VectorXf> target_vec(output_embeddings[b].data(), hidden_dim);
            
            // Point to the specific sentence data within the batch
            float* sentence_ptr = raw_data + (b * stride_batch);
            
            if (strategy == PoolingStrategy::MEAN) {
                // Map the VALID tokens as a Matrix [valid_len, hidden_dim]
                // Note: CT2 storage is RowMajor. Eigen defaults to ColMajor, so we must specify RowMajor.
                Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
                mat(sentence_ptr, valid_len, hidden_dim);
                
                // Vectorized Mean: Sum columns (colwise), then divide
                target_vec = mat.colwise().sum() / static_cast<float>(valid_len);
            }
            else if (strategy == PoolingStrategy::CLS) {
                // Map first row
                Eigen::Map<Eigen::VectorXf> cls_vec(sentence_ptr, hidden_dim);
                target_vec = cls_vec;
            }
            else if (strategy == PoolingStrategy::LAST_TOKEN) {
                // Map last valid row
                float* last_ptr = sentence_ptr + ((valid_len - 1) * hidden_dim);
                Eigen::Map<Eigen::VectorXf> last_vec(last_ptr, hidden_dim);
                target_vec = last_vec;
            }
            
            // 6. L2 Normalization (Vectorized)
            if (l2_normalize) {
                target_vec.normalize(); // In-place normalization
            }
        }
        
        Json::Value rootNode(Json::objectValue);
        Json::Value embeddingsNode(Json::arrayValue);
        for (float val : output_embeddings[0]) {
            embeddingsNode.append(val);
        }
        rootNode["embedding"] = embeddingsNode;
        rootNode["index"] = 0;
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        
        return Json::writeString(writer, rootNode);
    }

private:
    std::unique_ptr<ctranslate2::Encoder> encoder_;
    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;
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
#define ARGS (OPTARG_T)L"m:e:i:o:sp:jt:bcldf:-h"
#define _atoi _wtoi
#define _atof _wtof
#else
#define ARGS "m:e:i:o:sp:jt:bcldf:-h"
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

static void parse_request_translate(const std::string &json,
                                    std::vector<std::string> &inputs,
                                    size_t *num_hypotheses,
                                    size_t *sampling_topk,
                                    size_t *beam_size,
                                    size_t *max_decoding_length,
                                    size_t *min_decoding_length,
                                    double *sampling_topp,
                                    double *repetition_penalty) {
    
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
            Json::Value input_node = root["text"];
            if(input_node.isString())
            {
                inputs.push_back(input_node.asString());
            }
            if(input_node.isArray())
            {
                for (Json::ValueIterator i = input_node.begin(); i != input_node.end(); ++i)
                {
                    Json::Value node = *i;
                    if(node.isString())
                    {
                        inputs.push_back(node.asString());
                    }
                    
                }
            }
            Json::Value num_hypotheses_node = root["num_hypotheses"];
            if(num_hypotheses_node.isNumeric())
            {
                *num_hypotheses = num_hypotheses_node.asDouble();
            }
            Json::Value sampling_topk_node = root["sampling_topk"];
            if(sampling_topk_node.isNumeric())
            {
                *sampling_topk = sampling_topk_node.asDouble();
            }
            Json::Value beam_size_node = root["beam_size"];
            if(beam_size_node.isNumeric())
            {
                *beam_size = beam_size_node.asDouble();
            }
            Json::Value max_decoding_length_node = root["max_decoding_length"];
            if(max_decoding_length_node.isNumeric())
            {
                *max_decoding_length = max_decoding_length_node.asDouble();
            }
            Json::Value min_decoding_length_node = root["min_decoding_length"];
            if(min_decoding_length_node.isNumeric())
            {
                *min_decoding_length = min_decoding_length_node.asDouble();
            }
            Json::Value sampling_topp_node = root["sampling_topp"];
            if(sampling_topp_node.isNumeric())
            {
                *sampling_topp = sampling_topp_node.asDouble();
            }
            Json::Value repetition_penalty_node = root["repetition_penalty"];
            if(repetition_penalty_node.isNumeric())
            {
                *repetition_penalty = repetition_penalty_node.asDouble();
            }
        }
    }
}

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

static void before_run_translate(
                                  const std::string& request_body,
                                 std::vector<std::string> &inputs,
                                 size_t *num_hypotheses,
                                 size_t *sampling_topk,
                                 size_t *beam_size,
                                 size_t *max_decoding_length,
                                 size_t *min_decoding_length,
                                 double *sampling_topp,
                                 double *repetition_penalty
                                  ) {
    parse_request_translate(request_body, inputs,
                            num_hypotheses,
                            sampling_topk,
                            beam_size,
                            max_decoding_length,
                            min_decoding_length,
                            sampling_topp,
                            repetition_penalty);
}

static void before_run_embeddings(
                                  const std::string& request_body,
                                  std::string &input
                                  ) {
    parse_request_embeddings(request_body, input);
}

#pragma mark -

int main(int argc, OPTARG_T argv[]) {
    
#ifdef WIN32
    std::wstring model_path_u16;
    std::wstring embedding_model_path_u16;
    std::wstring source_sp_path_u16;
#endif
    std::string model_path;           // -m
    std::string embedding_model_path; // -e
    std::string chat_template;        // -j
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
            case 'f':
#ifdef WIN32
                source_sp_path_u16 = optarg;
                source_sp_path = wchar_to_utf8(source_sp_path_u16.c_str());
#else
                source_sp_path = optarg;
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
    
    std::string fingerprint;
    long long model_created = 0;
    std::string modelName;
    
    std::unique_ptr<TranslationService> translation_pipeline;
    
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
                    translation_pipeline = std::make_unique<TranslationService>(model_path, source_sp_path);
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
                    pipeline = std::make_unique<EmbeddingPipeline>(embedding_model_path);
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
        
        // Route: /v1/translate
        svr.Post("/v1/translate", [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/translate request received." << std::endl;
            
            try {
                
                size_t num_hypotheses = 1;
                size_t sampling_topk = 40;
                size_t beam_size = 50;
                size_t max_decoding_length = 512;
                size_t min_decoding_length = 1;
                double sampling_topp = 1;
                double repetition_penalty = 1.0;

                std::vector<std::string> texts;
                before_run_translate(req.body, texts,
                                     &num_hypotheses,
                                     &sampling_topk,
                                     &beam_size,
                                     &max_decoding_length,
                                     &min_decoding_length,
                                     &sampling_topp,
                                     &repetition_penalty);
                
                // --- Extract Translation Parameters ---
                ctranslate2::TranslationOptions options;
                // Map common JSON parameters to CTranslate2 options
                options.num_hypotheses = num_hypotheses;
                options.sampling_topk = sampling_topk;
                options.beam_size = beam_size;
                options.max_decoding_length = max_decoding_length;
                options.sampling_topp = sampling_topp;
                options.repetition_penalty = repetition_penalty;
 
                std::string response_json = translation_pipeline->translate_batch(texts, options);
                
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
                 
        // Route: /v1/embeddings
        svr.Post("/v1/embeddings", [&](const httplib::Request& req, httplib::Response& res) {
            
            std::cout << "[Server] /v1/embeddings request received." << std::endl;
            
            try {
                if(embedding_model_created == 0) {
                    throw std::invalid_argument("[Embedding] Model not loaded.");
                }
                std::string text;
                before_run_embeddings(req.body, text);
                std::string response_json = pipeline->embed_batch({text}, pooling_mode, true);
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
            std::string response = pipeline->embed_batch({text}, pooling_mode, true);
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
