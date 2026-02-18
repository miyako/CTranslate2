# ct2-server

CTranslate2 Inference Engine

```
Usage:  ct2-server -s -e embedding_model -p port 

 -m path     : translation model 
 -e path     : embedding model (pooling=mean)
 -r path     : reranker model
 -f path     : source sentencepiece model
 -l          : pooling=last-token (Llama)
 -c          : pooling=cls (Qwen)
 -s          : server (OpenAI compatible endpoint)
 -p          : server listening port (default=8080)
 -h host     : server host (default=127.0.0.1)    
```

## Dependencies

- `ctranslate2-4.7.1`
 
## OpenAI Compatible Endpoints

- `/v1/models`
- `/v1/chat/completions` (not implemented)
- `/v1/embeddings`

## Cohere Compatible Endpoints

- `/v1/rerank`

## Converted CT2 Models

#### Quantisation

The `int8_float16` format is primarily designed for **NVIDIA GPUs**. It stores weights in 8-bit integers but converts them to 16-bit floating point for maximum efficiency (storage+speed). CTranslate2 falls back to `float32` if CUDA is unavailable, which defeats the purpose of this hybrid format. 

The `float16` format is also designed for GPUs that support native 16-bit maths. The CPU backend of **CTranslate2** usually performs calculations in `float32` even on a CPU like Apple Silicon that actually has native 16-bit maths. The weights are automatically converted to 32-bit at startup.

The `int8` format takes advantage of `NEON` instructions on Apple Silicon and `AVX2` `AVX-512` `VNNI` instructions on Intel or AMD to **accelerate maths**. **You should always use the `int8` format on a PC or Mac with no GPU**.

### Rerank

||`int8`|`int8_float16`|`float16`|`max_position_embeddings`|`hidden_size`|`num_hidden_layers`
|-|-:|-:|-:|-:|-:|-:
|[`cross-encoder/ms-marco-MiniLM-L6-v2`](https://huggingface.co/cross-encoder/ms-marco-MiniLM-L6-v2)|[`23`](https://huggingface.co/keisuke-miyako/ms-marco-MiniLM-L6-v2-ct2-int8)|[`23`](https://huggingface.co/keisuke-miyako/ms-marco-MiniLM-L6-v2-ct2-int8_float16)|[`45`](https://huggingface.co/keisuke-miyako/ms-marco-MiniLM-L6-v2-ct2-float16)|`512`|`384`|`6`
|[`cross-encoder/mmarco-mMiniLMv2-L12-H384-v1`](https://huggingface.co/cross-encoder/mmarco-mMiniLMv2-L12-H384-v1)|[`119`](https://huggingface.co/keisuke-miyako/mmarco-mMiniLMv2-L12-H384-v1-ct2-int8)|[`119`](https://huggingface.co/keisuke-miyako/mmarco-mMiniLMv2-L12-H384-v1-ct2-int8_float16)|[`235`](https://huggingface.co/keisuke-miyako/mmarco-mMiniLMv2-L12-H384-v1-ct2-float16)|`512`|`384`|`12`|
|[`BAAI/bge-reranker-v2-m3`](https://huggingface.co/BAAI/bge-reranker-v2-m3)|[`594`](https://huggingface.co/keisuke-miyako/bge-reranker-v2-m3-ct2-int8)|[`577`](https://huggingface.co/keisuke-miyako/bge-reranker-v2-m3-ct2-int8_float16)|[`1130`](https://huggingface.co/keisuke-miyako/bge-reranker-v2-m3-ct2-float16)|`8192`|`1024`|`24`|
|[`BAAI/bge-reranker-base`](https://huggingface.co/BAAI/bge-reranker-base)|[`280`](https://huggingface.co/keisuke-miyako/bge-reranker-base-ct2-int8)|[`279`](https://huggingface.co/keisuke-miyako/bge-reranker-base-ct2-int8_float16)|[`555`](https://huggingface.co/keisuke-miyako/bge-reranker-base-ct2-float16)|`8192`|`768`|`12`|
|[`BAAI/bge-reranker-large`](https://huggingface.co/BAAI/bge-reranker-large)|[`563`](https://huggingface.co/keisuke-miyako/bge-reranker-large-ct2-int8)|[`561`](https://huggingface.co/keisuke-miyako/bge-reranker-large-ct2-int8_float16)|[`1120`](https://huggingface.co/keisuke-miyako/bge-reranker-large-ct2-float16)|`8192`|`1024`|`24`
|[`jinaai/jina-reranker-v3`](https://huggingface.co/jinaai/jina-reranker-v3)|[`598`](https://huggingface.co/keisuke-miyako/jina-reranker-v3-ct2-int8)|[`598`](https://huggingface.co/keisuke-miyako/jina-reranker-v3-ct2-int8_float16)|[`1190`](https://huggingface.co/keisuke-miyako/jina-reranker-v3-ct2-float16)|`131072`|`1024`|`28`|
|[`Qwen/Qwen3-Reranker-0.6B`](https://huggingface.co/Qwen/Qwen3-Reranker-0.6B)|[`597`](https://huggingface.co/keisuke-miyako/Qwen3-Reranker-0.6B-ct2-int8)|[`597`](https://huggingface.co/keisuke-miyako/Qwen3-Reranker-0.6B-ct2-int8_float16)|[`1190`](https://huggingface.co/keisuke-miyako/Qwen3-Reranker-0.6B-ct2-float16)|`32768`|`1024`|`28`|
|[`Qwen/Qwen3-Reranker-4B`](https://huggingface.co/Qwen/Qwen3-Reranker-4B)|[``](https://huggingface.co/keisuke-miyako/Qwen3-Reranker-4B-ct2-int8)|[``](https://huggingface.co/keisuke-miyako/Qwen3-Reranker-4B-ct2-int8_float16)|[``](https://huggingface.co/keisuke-miyako/Qwen3-Reranker-4B-ct2-float16)|`40960`|`2560`|`36`|
|[`zeroentropy/zerank-2`](https://huggingface.co/zeroentropy/zerank-2)|[``](https://huggingface.co/keisuke-miyako/zerank-2-ct2-int8)|[``](https://huggingface.co/keisuke-miyako/zerank-2-ct2-int8_float16)|[``](https://huggingface.co/keisuke-miyako/zerank-2-ct2-float16)|`40960`|`2560`|`36`

- https://huggingface.co/keisuke-miyako/bge-small-en-v1.5-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/bge-base-en-v1.5-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/bge-large-en-v1.5-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/bge-m3-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/e5-small-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/e5-base-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/e5-large-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/multilingual-e5-small-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/multilingual-e5-base-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/multilingual-e5-large-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-s-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-l-v2.0-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/all-MiniLM-L6-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/all-MiniLM-L12-v2-ct2-int8_float16
- ~~[gte-multilingual-base](https://huggingface.co/keisuke-miyako/gte-multilingual-base-ct2-int8_float16)~~ ⚠️ **CTranslate2** can't use Decoder for embeddings
- ~~[gte-Qwen2-1.5B-instruct](https://huggingface.co/keisuke-miyako/gte-Qwen2-1.5B-instruct-ct2-int8_float16)~~ ⚠️ **CTranslate2** can't use Decoder for embeddings
- ~~[gte-Qwen2-7B-instruct](https://huggingface.co/keisuke-miyako/gte-Qwen2-7B-instruct-ct2-int8_float16)~~ ⚠️ **CTranslate2** can't use Decoder for embeddings
- ~~universal-sentence-encoder~~ ⚠️ **CTranslate2** doesn't support TensorFlow
- ~~universal-sentence-encoder-large~~ ⚠️ **CTranslate2** doesn't support TensorFlow
- ~~universal-sentence-encoder-multilingual~~ ⚠️ **CTranslate2** doesn't support TensorFlow
- ~~universal-sentence-encoder-multilingual-large~~ ⚠️ **CTranslate2** doesn't support TensorFlow
- https://huggingface.co/keisuke-miyako/granite-embedding-30m-english-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/granite-embedding-125m-english-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/granite-embedding-107m-multilingual-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/granite-embedding-278m-multilingual-ct2-int8_float16
- ~~[sarashina-embedding-v1-1b](https://huggingface.co/keisuke-miyako/sarashina-embedding-v1-1b-ct2-int8_float16)~~ ⚠️ **CTranslate2** can't use Decoder for embeddings
- ~~[sarashina-embedding-v2-1b](https://huggingface.co/keisuke-miyako/sarashina-embedding-v2-1b-ct2-int8_float16)~~ ⚠️ **CTranslate2** can't use Decoder for embeddings
- ~~ruri-base-v2~~ ⚠️ **CTranslate2** doesn't support BertJapaneseTokenizer
- ~~ruri-large-v2~~ ⚠️ **CTranslate2** doesn't support BertJapaneseTokenizer

As of February 2026, **CTranslate2** does not support several notable model architectures :

#### NomicBertModel

- nomic-embed-text-v1
- nomic-embed-text-v1.5

#### NewModel

- gte-base-en-v1.5
- gte-large-en-v1.5

#### [Gemma3TextModel](https://github.com/OpenNMT/CTranslate2/issues/1866)

- embeddinggemma-300m

#### [ModernBERT](https://github.com/OpenNMT/CTranslate2/issues/1837) 

- ruri-v3-30m 
- ruri-v3-70m 
- ruri-v3-130m 
- ruri-v3-310m 
- modernbert-ja-30m 
- modernbert-ja-70m 
- modernbert-ja-130m 
- modernbert-ja-310m
- gte-modernbert-base
- amber-base
- amber-large
- granite-embedding-small-english-r2
- granite-embedding-english-r2
