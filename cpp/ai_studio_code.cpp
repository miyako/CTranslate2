#include <iostream>
#include <vector>
#include <string>
#include <numeric>
#include <cmath>

// CTranslate2 & SentencePiece headers
#include <ctranslate2/generator.h>
#include <sentencepiece_processor.h>

// Eigen for vector math
#include <Eigen/Dense>

// --------------------------------------------------------------------------
// Helper: Manual Chat Template (Since we don't have Python's transformers lib)
// This example uses the Llama-3 format. Adjust for your specific model.
// --------------------------------------------------------------------------
std::string apply_chat_template(const std::string& user_input) {
    // Format: <|begin_of_text|><|start_header_id|>user<|end_header_id|>\n\n{input}<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n
    std::string prompt = "<|begin_of_text|><|start_header_id|>user<|end_header_id|>\n\n" 
                         + user_input 
                         + "<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n";
    return prompt;
}

// --------------------------------------------------------------------------
// Part 1: Chat Completion Implementation
// --------------------------------------------------------------------------
void run_chat_completion(const std::string& model_path, sentencepiece::SentencePieceProcessor& tokenizer, const std::string& text) {
    std::cout << "--- Chat Completion ---\n";

    // 1. Initialize Generator (loads model into VRAM/RAM)
    //    device="cuda" for GPU, "cpu" for CPU.
    ctranslate2::Generator generator(model_path, "cuda");

    // 2. Format Input (Apply Chat Template)
    std::string formatted_prompt = apply_chat_template(text);

    // 3. Tokenize
    std::vector<std::string> tokens;
    tokenizer.Encode(formatted_prompt, &tokens);

    // 4. Generate
    ctranslate2::GenerationOptions options;
    options.max_length = 512;
    options.sampling_temperature = 0.7;
    
    // generate_batch takes a batch of prompts (vector of vector of strings)
    std::vector<ctranslate2::GenerationResult> results = generator.generate_batch({tokens}, options);

    // 5. Decode output tokens back to text
    std::vector<std::string> output_tokens = results[0].sequences[0];
    std::string output_text;
    tokenizer.Decode(output_tokens, &output_text);

    std::cout << "User: " << text << "\n";
    std::cout << "AI:   " << output_text << "\n\n";
}

// --------------------------------------------------------------------------
// Part 2: Mean Pooling & L2 Normalization (Your Specific Math Request)
// --------------------------------------------------------------------------
// This is typically done on an ENCODER model (like BERT) or hidden states
// to get a vector representation of the text.
void run_embedding_pipeline(const std::string& text) {
    std::cout << "--- Embedding (Mean Pool + L2 Norm) ---\n";
    
    // Simulating hidden states coming from CTranslate2 (Batch=1, SeqLen=3, HiddenDim=4)
    // In reality, you'd get this from: encoder.forward_batch(tokens).hidden_states
    int seq_len = 3;
    int hidden_dim = 4;
    
    // Mock data: [ [1,0,1,0], [0,2,0,2], [1,1,1,1] ]
    Eigen::MatrixXf hidden_states(seq_len, hidden_dim);
    hidden_states << 1, 0, 1, 0,
                     0, 2, 0, 2,
                     1, 1, 1, 1;

    std::cout << "Raw Hidden States:\n" << hidden_states << "\n\n";

    // --- STEP 1: Mean Pooling ---
    // Calculate the mean across the sequence dimension (rows) -> Result is 1x4 vector
    Eigen::VectorXf sentence_embedding = hidden_states.colwise().mean();
    
    std::cout << "Mean Pooled Vector:\n" << sentence_embedding.transpose() << "\n";

    // --- STEP 2: L2 Normalization ---
    // Eigen's .normalized() method performs L2 normalization
    Eigen::VectorXf final_embedding = sentence_embedding.normalized();

    std::cout << "L2 Normalized Vector:\n" << final_embedding.transpose() << "\n";
}

int main() {
    // Path to your converted CTranslate2 model and tokenizer file
    std::string model_path = "llama-3-8b-ct2-int8"; 
    std::string sp_model_path = "tokenizer.model";

    // Load Tokenizer (SentencePiece)
    sentencepiece::SentencePieceProcessor tokenizer;
    const auto status = tokenizer.Load(sp_model_path);
    if (!status.ok()) {
        std::cerr << "Failed to load tokenizer: " << status.toString() << std::endl;
        // In a real app, handle error or exit. 
        // For this example, we proceed to the mock embedding part.
    } else {
        run_chat_completion(model_path, tokenizer, "Explain how C++ is faster than Python.");
    }

    run_embedding_pipeline("Explain how C++ is faster than Python.");

    return 0;
}