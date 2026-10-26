---
layout: default
title: "Embeddings"
---

# Embeddings

`/v1/embeddings` `/v1/rerank` `v1/contextualizedembeddings`

## RoBERTa

The native build's CTranslate2 v4.8.4 supports RoBERTa encoders. Convert with
`ct2-transformers-converter`, copy `tokenizer.json`, and save the original Hugging Face
configuration as **`source_config.json`**, leaving CT2's own `config.json` intact.
Start with `ct2-server -s -e /path/to/roberta-ct2`.

The server selects RoBERTa's `<s> text </s>` token layout, obtains special-token IDs
from the source metadata, and accounts for RoBERTa's padding offset when limiting
the input length (514 learned positions normally allow 512 input tokens).
Mean pooling is the default; use `-c` for CLS or `-l` for last-token pooling.
Padding is excluded from pooling, and output vectors are L2-normalized.
This does not add a sentence-transformers projection head: use an embedding-trained
checkpoint and its prescribed pooling for useful semantic similarity.

RoBERTa pair reranking retains `<s> query </s></s> document </s>` formatting, but
requires the separately exported classifier weights expected by the existing reranker.
The upstream `RobertaModel` converter alone does not supply a task-specific classifier.

Qwen 3, Gemma 3/4, T5Gemma2, and UMT5 cannot be loaded as `Encoder` models for
embeddings or classifier reranking. Such configurations now fail explicitly instead
of defaulting to RoBERTa token IDs. Decoder hidden-state extraction, Qwen3-Embedding,
EmbeddingGemma, and LLM yes/no reranking are not implemented by this server.
The contextualized endpoint still pools concatenated context/chunk text; it is not
a native contextualized embedding architecture.
