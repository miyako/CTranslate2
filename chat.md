---
layout: default
title: "Chat"
---

# Chat

`/v1/chat/completion` uses `ctranslate2::GenerationOptions`

```c
struct GenerationOptions {
    // Beam size to use for beam search (set 1 to run greedy search).
    size_t beam_size = 1;
    // Beam search patience factor, as described in https://arxiv.org/abs/2204.05424.
    // The decoding will continue until beam_size*patience hypotheses are finished.
    float patience = 1;
    // Exponential penalty applied to the length during beam search.
    // The scores are normalized with:
    //   hypothesis_score /= (hypothesis_length ** length_penalty)
    float length_penalty = 1;
    // Penalty applied to the score of previously generated tokens, as described in
    // https://arxiv.org/abs/1909.05858 (set > 1 to penalize).
    float repetition_penalty = 1;
    // Prevent repetitions of ngrams with this size (set 0 to disable).
    size_t no_repeat_ngram_size = 0;
    // Disable the generation of the unknown token.
    bool disable_unk = false;
    // Disable the generation of some sequences of tokens.
    std::vector<std::vector<std::string>> suppress_sequences;

    // Stop the decoding on one of these tokens (defaults to the model EOS token).
    std::variant<std::string, std::vector<std::string>, std::vector<size_t>> end_token;

    // Include the end token in the result.
    bool return_end_token = false;

    // Length constraints.
    size_t max_length = 512;
    size_t min_length = 0;

    // Randomly sample from the top K candidates (set 0 to sample from the full output distribution).
    size_t sampling_topk = 1;
    // Keep the most probable tokens whose cumulative probability exceeds this value.
    float sampling_topp = 1;
    // High temperature increase randomness.
    float sampling_temperature = 1;

    // Number of hypotheses to include in the result.
    size_t num_hypotheses = 1;

    // Include scores in the result.
    bool return_scores = false;
    // Include log probs of each token in the result
    bool return_logits_vocab = false;

    // Return alternatives at the first unconstrained decoding position. This is typically
    // used with a prefix to provide alternatives at a specifc location.
    bool return_alternatives = false;
    // Minimum probability to expand an alternative.
    float min_alternative_expansion_prob = 0;

    // The static prompt will prefix all inputs for this model.
    std::vector<std::string> static_prompt;

    // Cache the model state after the static prompt and reuse it for future runs using
    // the same static prompt.
    bool cache_static_prompt = true;

    // Include the input tokens in the generation result.
    bool include_prompt_in_result = true;

    // Function to call for each generated token in greedy search.
    // Returns true indicate the current generation is considered finished thus can be stopped early.
    std::function<bool(GenerationStepResult)> callback = nullptr;
};
```
