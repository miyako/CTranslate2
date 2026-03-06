//
//  main.cpp
//  ct2-server
//
//  Created by miyako on 2026/01/09.
//

#include "ct2-server.h"

static int GetOptimalIntraOpThreads() {
    int threads = 0;

    // --- macOS Implementation ---
    #if defined(__APPLE__)
        int32_t core_count = 0;
        size_t size = sizeof(core_count);
        
        // 1. Try to get "Performance Level 0" cores (P-Cores on Apple Silicon)
        // This is critical for M1/M2/M3 to avoid using slow E-Cores.
        if (sysctlbyname("hw.perflevel0.physicalcpu", &core_count, &size, NULL, 0) == 0) {
            threads = core_count;
        }
        // 2. Fallback: Standard Physical Cores (Intel Mac or if perflevel fails)
        else if (sysctlbyname("hw.physicalcpu", &core_count, &size, NULL, 0) == 0) {
            threads = core_count;
        }
        else {
            // Absolute fallback
            threads = std::thread::hardware_concurrency();
        }

    // --- Windows Implementation ---
    #elif defined(_WIN32)
        // Getting strictly physical cores on Windows is complex (requires iterating SYSTEM_LOGICAL_PROCESSOR_INFORMATION).
        // For a simple implementation, hardware_concurrency (Logical Cores) is often acceptable,
        // but dividing by 2 is a common heuristic for Hyper-threaded Intel/AMD CPUs to estimate physical cores.
        
        unsigned int logical_cores = std::thread::hardware_concurrency();
        // Heuristic: If we have many cores, assume Hyper-threading and divide by 2.
        // Otherwise, use all.
        if (logical_cores > 4) {
            threads = logical_cores / 2;
        } else {
            threads = logical_cores;
        }

    // --- Linux / Generic Implementation ---
    #else
        // Similar heuristic for Linux
        unsigned int logical_cores = std::thread::hardware_concurrency();
        if (logical_cores > 4) {
             threads = logical_cores / 2;
        } else {
             threads = logical_cores;
        }
    #endif

    // Safety clamp: Ensure we have at least 1 thread and not an insane amount (cap at 16 for client devices)
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

namespace fs = std::filesystem;
using namespace tokenizers;

static // Helper: Read entire file into a string (Blob)
std::string LoadBytesFromFile(const std::string& path) {
    std::ifstream fs(path, std::ios::in | std::ios::binary);
    if (!fs) throw std::runtime_error("Could not open file: " + path);
    
    std::string data((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    return data;
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

static // Helper to read the template file from the model directory
RerankingMode LoadRerankingMode(const std::string& model_path) {
    fs::path path(model_path);
    fs::path config_path = path;

    if (fs::is_directory(path)) {
        config_path = path / "config.json";
    }
    
    if (fs::exists(config_path) && config_path.extension() == ".json") {
//        std::cout << "Loading model_type from: " << config_path << std::endl;
        
        std::string json = LoadBytesFromFile(config_path.string());
        
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
                Json::Value model_type_node = root["model_type"];
                if(model_type_node.isString())
                {
                    std::string model_type = model_type_node.asString();
                    // RERANKING_ROBERTA
                    if(model_type == "xlm-roberta") {
                        std::cout << "[Rerank] model_type: " << model_type << " (roberta)" << std::endl;
                        return RERANKING_ROBERTA;
                    }
                    if(model_type == "roberta") {
                        std::cout << "[Rerank] model_type: " << model_type << std::endl;
                        return RERANKING_ROBERTA;
                    }
                    if(model_type == "camembert") {
                        std::cout << "[Rerank] model_type: " << model_type << " (roberta)" << std::endl;
                        return RERANKING_ROBERTA;
                    }
                    // RERANKING_BERT
                    if(model_type == "bert") {
                        std::cout << "[Rerank] model_type: " << model_type << " (bert)" << std::endl;
                        return RERANKING_BERT;
                    }
                    if(model_type == "mpnet") {
                        std::cout << "[Rerank] model_type: " << model_type << " (bert)" << std::endl;
                        return RERANKING_BERT;
                    }
                    if(model_type == "deberta-v2") {
                        std::cout << "[Rerank] model_type: " << model_type << " (bert)" << std::endl;
                        return RERANKING_BERT;
                    }
                    if(model_type == "modernbert") {
                        std::cout << "[Rerank] model_type: " << model_type << " (bert)" << std::endl;
                        return RERANKING_BERT;
                    }
                    if(model_type == "qwen3") {
                        std::cout << "[Rerank] model_type: " << model_type << " (llm)" << std::endl;
                        return RERANKING_LLM;
                    }
                    if(model_type == "qwen2") {
                        std::cout << "[Rerank] model_type: " << model_type << " (llm)" << std::endl;
                        return RERANKING_LLM;
                    }
                    if(model_type == "mistral") {
                        std::cout << "[Rerank] model_type: " << model_type << " (llm)" << std::endl;
                        return RERANKING_LLM;
                    }
                    if(model_type == "llama") {
                        std::cout << "[Rerank] model_type: " << model_type << " (llm)" << std::endl;
                        return RERANKING_LLM;
                    }
                    if(model_type == "gemma") {
                        std::cout << "[Rerank] model_type: " << model_type << " (llm)" << std::endl;
                        return RERANKING_LLM;
                    }
                    if(model_type == "gemma2") {
                        std::cout << "[Rerank] model_type: " << model_type << " (llm)" << std::endl;
                        return RERANKING_LLM;
                    }
                    if(model_type == "phi3") {
                        std::cout << "[Rerank] model_type: " << model_type << " (llm)" << std::endl;
                        return RERANKING_LLM;
                    }
                    //
                }
            }
        }
    }
    
    std::cout << "[Rerank] model_type: default (roberta)" << std::endl;
    return RERANKING_ROBERTA;
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

class RerankerPipeline {
    public:
    RerankerPipeline(const std::string& model_dir,
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
        
        try {
            ctranslate2::Device device_type = (device == "cuda") ?
            ctranslate2::Device::CUDA : ctranslate2::Device::CPU;
            encoder_ = std::make_unique<ctranslate2::Encoder>(model_dir, device_type);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to load CTranslate2 model: " + std::string(e.what()));
        }
        
        LoadRerankHead(model_dir);
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
                case RERANKING_ROBERTA:
                    ids.reserve(q_ids.size() + doc_ids.size() + 4);
                    ids.push_back(0); // <s>
                    ids.insert(ids.end(), q_ids.begin(), q_ids.end());
                    ids.push_back(2); // </s>
                    ids.push_back(2); // </s>
                    ids.insert(ids.end(), doc_ids.begin(), doc_ids.end());
                    ids.push_back(2); // </s>
                    type_ids.resize(ids.size(), 0);
                    break;
                case RERANKING_BERT:
                    ids.reserve(q_ids.size() + doc_ids.size() + 3);
                    type_ids.reserve(ids.capacity());
                    ids.push_back(101); // [CLS]
                    type_ids.push_back(0);
                    for(int x : q_ids) { ids.push_back(x); type_ids.push_back(0); }
                    ids.push_back(102); // [SEP]
                    type_ids.push_back(0);
                    for(int x : doc_ids) { ids.push_back(x); type_ids.push_back(1); }
                    ids.push_back(102); // [SEP]
                    type_ids.push_back(1);
                    break;
                case RERANKING_LLM:
                default:
                    ids = tokenizer_->Encode(query + "\n" + documents[i]);
                    type_ids.resize(ids.size(), 0);
                    break;
            }
            
            if (ids.size() > max_position_embeddings_) {
                int end_token = ids.back();
                size_t end_type_id = type_ids.back();
                ids.resize(max_position_embeddings_ - 1);
                ids.push_back(end_token);
                type_ids.resize(max_position_embeddings_ - 1);
                type_ids.push_back(end_type_id);
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
                    Eigen::VectorXf dense_out = (embedding.transpose() * dense_weights_).transpose();
                    dense_out += dense_bias_;
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
            float max_logit = -std::numeric_limits<float>::infinity();
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
                    Eigen::VectorXf dense_out = (embedding.transpose() * dense_weights_).transpose();
                    dense_out += dense_bias_;
                    // Layer 2: Activation (Tanh)
                    dense_out = dense_out.unaryExpr([](float x) { return std::tanh(x); });
                    // Layer 3: Out Projection (Linear)
                    logits = dense_out.dot(out_weights_) + out_bias_;
                }
                else {
                    // Simple Linear Head (MiniLM style)
                    logits = embedding.dot(out_weights_) + out_bias_;
                }
                float score = 1.0f / (1.0f + std::exp(-logits));
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
            
            // 2. Read Hidden Dimension
            int32_t dim = 0;
            file.read(reinterpret_cast<char*>(&dim), sizeof(int32_t));

            if (version == 2) {
                has_dense_layer_ = true;
                
                // Allocate and Read Dense Weights (Matrix [Dim x Dim])
                // PyTorch stores weights as [OutFeatures, InFeatures].
                // We want to perform: Vec(1xD) * Mat(DxD).
                // So we need to be careful with Transpose.
                // PyTorch Linear(x) = x * W^T + b.
                // So the file contains W (Out x In). Since Out=Dim and In=Dim.
                // We read it into a flat buffer.
                std::vector<float> dense_w_buf(dim * dim);
                file.read(reinterpret_cast<char*>(dense_w_buf.data()), dense_w_buf.size() * sizeof(float));
                
                std::vector<float> dense_b_buf(dim);
                file.read(reinterpret_cast<char*>(dense_b_buf.data()), dim * sizeof(float));
                
                // Map to Eigen
                // RowMajor is important here because PyTorch exports row-by-row.
                // MatrixXf is ColMajor by default.
                // We load it as RowMajor, then let Eigen handle it.
                dense_weights_ = Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>::Map(dense_w_buf.data(), dim, dim);
                
                // To compute x * W^T, we can just do x * W.transpose()
                // Or if we loaded W as [Out, In], x * W^T is equivalent to W * x if x is col vector.
                // Let's stick to standard math: dense_weights_ is [Dim, Dim].
                dense_weights_.transposeInPlace(); // Prepare for x * W multiplication style
                
                dense_bias_ = Eigen::VectorXf::Map(dense_b_buf.data(), dim);
            }

            // 3. Read Out Layer
            file.read(reinterpret_cast<char*>(&out_bias_), sizeof(float));
            
            std::vector<float> out_w_buf(dim);
            file.read(reinterpret_cast<char*>(out_w_buf.data()), dim * sizeof(float));
            out_weights_ = Eigen::VectorXf::Map(out_w_buf.data(), dim);
        }
};

class TranslationPipeline {
    public:
    TranslationPipeline(const std::string& model_dir,
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
#ifdef WIN32
        std::string s = wchar_to_utf8(sp_model_path.c_str());
const auto status = tokenizer_->Load(s.c_str());
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
            const ctranslate2::TranslationResult result = results.at(i);
            Json::Value translationNode(Json::objectValue);
            if(result.scores.size() != 0) {
                translationNode["score"] = result.scores[0];
            }else{
                translationNode["score"] = Json::nullValue;
            }
            Json::Value hypothesesNode(Json::arrayValue);
            for (const auto& hyp : result.hypotheses) {
                std::string detokenized;
                tokenizer_->Decode(hyp, &detokenized);
                hypothesesNode.append(detokenized);
            }
            translationNode["text"] = hypothesesNode;
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
        
        max_position_embeddings_ = LoadMaxPositionEmbeddings(model_dir);
        
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
        if (texts.empty()) return "{}";
                
        // 1. Tokenize & Prepare Batch
        std::vector<std::vector<size_t>> batch_ids;
        //        std::vector<size_t> lengths;
        batch_ids.reserve(texts.size());
        //        lengths.reserve(texts.size());
        
        for (const auto& text : texts) {
            auto ids_int = tokenize_one(text);
            
            std::vector<size_t> ids_size_t;
            size_t input_len = ids_int.size();
            // Truncate if necessary to fit [CLS] ... [SEP]
            if (input_len > max_position_embeddings_ - 2) {
                input_len = max_position_embeddings_ - 2;
            }
            ids_size_t.reserve(input_len + 2);
            
            // Add Special Tokens (Manual construction based on prompt logic)
            if(strategy == PoolingStrategy::CLS) ids_size_t.push_back(0); // [CLS] (101 for bert, 0 for some)
            for(size_t i=0; i<input_len; ++i) ids_size_t.push_back(static_cast<size_t>(ids_int[i]));
            if(strategy == PoolingStrategy::CLS) ids_size_t.push_back(2); // [SEP]
            
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
        long max_seq_len = shape[1]; // Padded length of this batch
        long hidden_dim = shape[2];
        long stride_batch = max_seq_len * hidden_dim;
        
        // MATH OPTIMIZATION 1: Single Flat Allocation
        // Allocating N vectors is slow. Allocate one big block for the results.
        std::vector<float> all_embeddings(batch_size * hidden_dim);
        
        // 4. Compute Pooling
        for (long b = 0; b < batch_size; ++b) {
            // Get valid length for this specific sentence (excluding padding, created in step 1)
            long valid_len = batch_ids[b].size();
            if (valid_len == 0) valid_len = 1; // Safety
            
            // Map the destination memory for this batch item
            // Using Map allows Eigen to write directly into our pre-allocated 'all_embeddings'
            Eigen::Map<Eigen::VectorXf> target_vec(all_embeddings.data() + (b * hidden_dim), hidden_dim);
            
            // Pointer to start of this sentence in CT2 output
            float* sentence_ptr = raw_data + (b * stride_batch);
            
            if (strategy == PoolingStrategy::MEAN) {
                // MATH OPTIMIZATION 2: Cache-Friendly Accumulation
                // CT2 output is RowMajor [Seq, Dim].
                // Previous code used colwise().sum().
                // On RowMajor data, accessing columns is strided (slow cache access).
                // Faster approach: Read row by row (sequential) and add to accumulator.
                
                // 1. Map the valid matrix portion [valid_len, hidden_dim]
                Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
                mat(sentence_ptr, valid_len, hidden_dim);
                
                // 2. Initialize target with zeros
                target_vec.setZero();
                
                // 3. Accumulate Rows (Sequential Memory Access)
                // target_vec += row makes Eigen iterate contiguous memory for the row
                for (long i = 0; i < valid_len; ++i) {
                    target_vec += mat.row(i);
                }
                
                // 4. Divide
                target_vec /= static_cast<float>(valid_len);
            }
            else if (strategy == PoolingStrategy::CLS) {
                // First token vector
                target_vec = Eigen::Map<Eigen::VectorXf>(sentence_ptr, hidden_dim);
            }
            else if (strategy == PoolingStrategy::LAST_TOKEN) {
                // Last token vector
                float* last_ptr = sentence_ptr + ((valid_len - 1) * hidden_dim);
                target_vec = Eigen::Map<Eigen::VectorXf>(last_ptr, hidden_dim);
            }
            // 5. L2 Normalization
            if (l2_normalize) {
                // Eigen uses SIMD instructions here if -march=native is enabled
                target_vec.normalize();
            }
        }
        
        // 5. Build JSON Response
        Json::Value rootNode(Json::objectValue);
        Json::Value listNode(Json::arrayValue);
        
        for (long b = 0; b < batch_size; ++b) {
            Json::Value dataNode(Json::objectValue);
            Json::Value embeddingsNode(Json::arrayValue);
            
            // Pointer arithmetic to get start of this embedding in flat vector
            size_t start_idx = b * hidden_dim;
            
            // Note: converting float to json value is costly, but unavoidable here
            for (long i = 0; i < hidden_dim; ++i) {
                embeddingsNode.append(all_embeddings[start_idx + i]);
            }
            
            dataNode["embedding"] = embeddingsNode;
            dataNode["index"] = (int)b;
            listNode.append(dataNode);
        }
        
        rootNode["data"] = listNode;
        rootNode["object"] = "list";
        
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        
        return Json::writeString(writer, rootNode);
    }

private:
    std::unique_ptr<ctranslate2::Encoder> encoder_;
    std::unique_ptr<tokenizers::Tokenizer> tokenizer_;

    int max_position_embeddings_;
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
    fprintf(stderr, "Usage:  ct2-server -s -e embedding_model -p port\n\n");
    fprintf(stderr, " -%c path     : %s\n", 'm' , "translation model");
    fprintf(stderr, " -%c path     : %s\n", 'e' , "embedding model (pooling=mean)");
    fprintf(stderr, " -%c path     : %s\n", 'r' , "reranker model");
    fprintf(stderr, " -%c path     : %s\n", 'f' , "source sentencepiece model");
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
#define ARGS (OPTARG_T)L"m:e:r:f:i:o:sp:jt:bcld-h"
#define _atoi _wtoi
#define _atof _wtof
#else
#define ARGS "m:e:r:f:i:o:sp:jt:bcld-h"
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

static void parse_request_reranking(const std::string &json,
                                     std::string &query,
                                     int *top_n,
                                     std::vector<std::string> &documents
                                     ) {
    
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

static void parse_request_embeddings(const std::string &json,
                                     std::vector<std::string> &inputs) {
    
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

static void before_run_reranking(
                                 const std::string& request_body,
                                 std::string &query,
                                 int *top_n,
                                 std::vector<std::string> &documents
                                 ) {
    parse_request_reranking(request_body, query, top_n, documents);
}

static void before_run_embeddings(
                                  const std::string& request_body,
                                  std::vector<std::string> &inputs
                                  ) {
    parse_request_embeddings(request_body, inputs);
}

#pragma mark -

int main(int argc, OPTARG_T argv[]) {
    
#ifdef WIN32
    std::wstring model_path_u16;
    std::wstring embedding_model_path_u16;
    std::wstring source_sp_path_u16;
    std::wstring reranker_model_path_u16;
#endif
    std::string model_path;           // -m
    std::string embedding_model_path; // -e
    std::string reranker_model_path;  // -r
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
            case 'r':
#ifdef WIN32
                reranker_model_path_u16 = optarg;
                reranker_model_path = wchar_to_utf8(reranker_model_path_u16.c_str());
#else
                reranker_model_path = optarg;
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
    
    int intra_op_threads = GetOptimalIntraOpThreads();
    std::cout << "Detected " << intra_op_threads << " Intra-Op threads." << std::endl;
    
    std::string fingerprint;
    long long model_created = 0;
    std::string modelName;
    
    std::unique_ptr<TranslationPipeline> translation_pipeline;
    
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
                    translation_pipeline = std::make_unique<TranslationPipeline>(model_path, source_sp_path, intra_op_threads);
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
                std::vector<std::string> texts;
                before_run_embeddings(req.body, texts);
                std::string response_json = pipeline->embed_batch(texts, pooling_mode);
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
        std::vector<std::string> texts;
        
        try {
            before_run_embeddings(request_str, texts);
            std::string response = pipeline->embed_batch(texts, pooling_mode);
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
