---
layout: default
title: "Chat"
---

# Chat

## Model families

The native build workflow uses [CTranslate2 v4.8.4](https://github.com/OpenNMT/CTranslate2/releases/tag/v4.8.4).
Start text chat with `ct2-server -s -g /path/to/ct2-model`.
Both `/v1/chat/completions` and `/v1/chat/completion` are accepted, including streaming and multiple choices.

| Family | Prompt handling |
|---|---|
| Qwen 3 (dense) | Native ChatML text chat; `enable_thinking: false` adds Qwen's non-thinking prefix |
| Gemma 3 / Gemma 3 Text | Native Gemma turns, BOS, and a leading system message merged into the first user turn |
| Gemma 4 / Gemma 4 Unified (text) | Native Gemma turns; format selected from the converted model's EOS token, including the newer `<\|turn>` / `<turn\|>` vocabulary |
| T5Gemma2 / UMT5 | Encoder-decoder generation through `-a` and `/v1/generate`, not the decoder-only chat endpoint |
| RoBERTa | Encoder embeddings through `-e`, not chat |

Model architecture support does not imply support for every checkpoint: use a variant that the
pinned upstream converter accepts. These endpoints are **text-only**; image, video, and audio
content parts are rejected. Base/pretrained models are not necessarily instruction-tuned.
Qwen 3 MoE and decoder-based embeddings (including EmbeddingGemma) are not enabled by this change.

The server reads Hugging Face metadata from **`source_config.json`**, falling back to legacy
`config.json` files that include `model_type`. Keep the original HF configuration under that
separate filename: do **not** replace the converted CT2 `config.json`.
For example, after conversion:

```sh
ct2-transformers-converter --model Qwen/Qwen3-0.6B --output_dir qwen3-ct2 \
  --quantization int8 --copy_files tokenizer.json tokenizer_config.json
python -c "from huggingface_hub import hf_hub_download; import shutil; shutil.copyfile(hf_hub_download('Qwen/Qwen3-0.6B', 'config.json'), 'qwen3-ct2/source_config.json')"
ct2-server -s -g qwen3-ct2
```

Use a converter matching the runtime version and a Transformers release supporting the source
architecture. Access to gated Google checkpoints requires accepting their license and authenticating.

```sh
curl http://127.0.0.1:8080/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{"messages":[{"role":"user","content":"Hello"}],"enable_thinking":false,"max_tokens":128}'
```

Native prompts deliberately cover text conversation only. For existing Qwen tool calling,
pass an **Inja-compatible** template with `-t template.jinja` or `-j` (stdin).
An explicit template overrides native formatting. Arbitrary Hugging Face Jinja templates are
not necessarily Inja-compatible; advanced macros, filters, and Python methods are not supported.
Without an explicit template, Qwen 3 / Gemma 3 / Gemma 4 use the native formatter rather than
trying to render the checkpoint's full Jinja template. Other families retain `chat_template.jinja`
loading. Invalid requests/templates now produce an error instead of attempting inference with
an empty or partial prompt. Native tool definitions/history are rejected; Gemma-specific tool
calling and separate OpenAI reasoning fields are not implemented (thinking remains in text).

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
