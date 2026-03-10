# ct2-server

CTranslate2 Inference Engine

```
Usage:  ct2-server -s -e embedding_model -p port 

 -m path     : translation model
 -f path     : source sentencepiece model
 -e path     : embedding model (pooling=mean)
 -r path     : reranker model
 -g path     : chat completion model
 -t path     : chat template
 -j          : chat template from stdin
 -l          : pooling=last-token (Llama)
 -c          : pooling=cls (Qwen)
 -s          : server (OpenAI compatible endpoint)
 -p          : server listening port (default=8080)
 -h host     : server host (default=127.0.0.1)    
```

The CLI is built for `4` platforms:

- macOS Apple Silicon
- macOS Intel
- Windows AMD
- Windows ARM

## Dependencies

- `ctranslate2-4.7.1`
 
## OpenAI Compatible Endpoints

- `/v1/models`
- `/v1/embeddings`
- `/v1/chat/completion`

## Cohere Compatible Endpoints

- `/v1/rerank`

## MongoDB Compatible Endpoints

- `/v1/contextualizedembeddings`
- `/v1/contextualized/embeddings` (alias)

## Converted CT2 Models

#### Quantisation

The `int8_float16` format is primarily designed for **NVIDIA GPUs**. It stores weights in 8-bit integers but converts them to 16-bit floating point for maximum efficiency (storage+speed). CTranslate2 falls back to `float32` if CUDA is unavailable, which defeats the purpose of this hybrid format. 

The `float16` format is also designed for GPUs that support native 16-bit maths. The CPU backend of **CTranslate2** usually performs calculations in `float32` even on a CPU like Apple Silicon that actually has native 16-bit maths. The weights are automatically converted to 32-bit at startup.

The `int8` format takes advantage of `NEON` instructions on Apple Silicon and `AVX2` `AVX-512` `VNNI` instructions on Intel or AMD to **accelerate maths**. **You should always use the `int8` format on a PC or Mac with no GPU**.

### Chat Completion

||`int8`|`max_position_embeddings`|`hidden_size`|`num_hidden_layers`
|-|-:|-:|-:|-:|
|[`elyza/ELYZA-japanese-Llama-2-7b-fast-instruct`](https://huggingface.co/elyza/ELYZA-japanese-Llama-2-7b-fast-instruct)|[`6850`](https://huggingface.co/keisuke-miyako/Hammer2.1-0.5b-onnx-int4)|`4096`|`4096`|`32`|
|[`Rakuten/RakutenAI-2.0-mini-instruct`](https://huggingface.co/Rakuten/RakutenAI-2.0-mini-instruct)|[`1540`](https://huggingface.co/keisuke-miyako/Hammer2.1-0.5b-onnx-int4)|`131072`|`2048`|`22`|
|[`Rakuten/RakutenAI-7B-instruct`](https://huggingface.co/Rakuten/RakutenAI-7B-instruct)|[`7380`](https://huggingface.co/keisuke-miyako/TinySwallow-1.5B-Instruct-ct2-int8)|`32768`|`4096`|`32`|
|[`llm-jp/llm-jp-3-1.8b-instruct`](https://huggingface.co/llm-jp/llm-jp-3-1.8b-instruct)|[`1870`](https://huggingface.co/keisuke-miyako/llm-jp-3-1.8b-instruct-ct2-int8)|`4096`|`2048`|`24`|
|[`llm-jp/llm-jp-3-3.7b-instruct`](https://huggingface.co/llm-jp/llm-jp-3-3.7b-instruct)|[`3790`](https://huggingface.co/keisuke-miyako/llm-jp-3-3.7b-instruct-ct2-int8)|`4096`|`3072`|`28`|
|[`SakanaAI/TinySwallow-1.5B-Instruct`](https://huggingface.co/SakanaAI/TinySwallow-1.5B-Instruct)|[`1550`](https://huggingface.co/keisuke-miyako/TinySwallow-1.5B-Instruct-ct2-int8)|`32768`|`1536`|`28`|
|[`sbintuitions/sarashina2.2-0.5b-instruct-v0.1`](https://huggingface.co/sbintuitions/sarashina2.2-0.5b-instruct-v0.1)|[`795`](https://huggingface.co/keisuke-miyako/TinySwallow-1.5B-Instruct-ct2-int8)|`8192`|`1280`|`24`|
|[`sbintuitions/sarashina2.2-1b-instruct-v0.1`](https://huggingface.co/sbintuitions/sarashina2.2-1b-instruct-v0.1)|[`1410`](https://huggingface.co/keisuke-miyako/TinySwallow-1.5B-Instruct-ct2-int8)|`8192`|`1792`|`24`|
|[`sbintuitions/sarashina2.2-3b-instruct-v0.1`](https://huggingface.co/sbintuitions/sarashina2.2-3b-instruct-v0.1)|[`3360`](https://huggingface.co/keisuke-miyako/sarashina2.2-3b-instruct-v0.1-ct2-int8)|`8192`|`2560`|`32`|
|[`google/gemma-2-2b-jpn-it`](https://huggingface.co/google/gemma-2-2b-jpn-it)|[`2620`](https://huggingface.co/keisuke-miyako/gemma-2-2b-jpn-it-ct2-int8)|`8192`|`2304`|`26`|
|[`tokyotech-llm/Gemma-2-Llama-Swallow-2b-it-v0.1`](https://huggingface.co/tokyotech-llm/Gemma-2-Llama-Swallow-2b-it-v0.1)|[`2620`](https://huggingface.co/keisuke-miyako/Gemma-2-Llama-Swallow-2b-it-v0.1-ct2-int8)|`8192`|`2304`|`26`|
|[`SakanaAI/Llama-3-Karamaru-v1`](https://huggingface.co/SakanaAI/Llama-3-Karamaru-v1)|[`8040`](https://huggingface.co/keisuke-miyako/Gemma-2-Llama-Swallow-2b-it-v0.1-ct2-int8)|`8192`|`4096`|`32`|

### Rerank

||`int8`|`int8_float16`|`float16`|`max_position_embeddings`|`hidden_size`|`num_hidden_layers`
|-|-:|-:|-:|-:|-:|-:
|[`cross-encoder/ms-marco-MiniLM-L6-v2`](https://huggingface.co/cross-encoder/ms-marco-MiniLM-L6-v2)|[`23`](https://huggingface.co/keisuke-miyako/ms-marco-MiniLM-L6-v2-ct2-int8)|[`23`](https://huggingface.co/keisuke-miyako/ms-marco-MiniLM-L6-v2-ct2-int8_float16)|[`45`](https://huggingface.co/keisuke-miyako/ms-marco-MiniLM-L6-v2-ct2-float16)|`512`|`384`|`6`
|[`cross-encoder/mmarco-mMiniLMv2-L12-H384-v1`](https://huggingface.co/cross-encoder/mmarco-mMiniLMv2-L12-H384-v1)|[`119`](https://huggingface.co/keisuke-miyako/mmarco-mMiniLMv2-L12-H384-v1-ct2-int8)|[`119`](https://huggingface.co/keisuke-miyako/mmarco-mMiniLMv2-L12-H384-v1-ct2-int8_float16)|[`235`](https://huggingface.co/keisuke-miyako/mmarco-mMiniLMv2-L12-H384-v1-ct2-float16)|`512`|`384`|`12`|
|[`BAAI/bge-reranker-v2-m3`](https://huggingface.co/BAAI/bge-reranker-v2-m3)|[`594`](https://huggingface.co/keisuke-miyako/bge-reranker-v2-m3-ct2-int8)|[`577`](https://huggingface.co/keisuke-miyako/bge-reranker-v2-m3-ct2-int8_float16)|[`1130`](https://huggingface.co/keisuke-miyako/bge-reranker-v2-m3-ct2-float16)|`8192`|`1024`|`24`|
|[`BAAI/bge-reranker-base`](https://huggingface.co/BAAI/bge-reranker-base)|[`280`](https://huggingface.co/keisuke-miyako/bge-reranker-base-ct2-int8)|[`279`](https://huggingface.co/keisuke-miyako/bge-reranker-base-ct2-int8_float16)|[`555`](https://huggingface.co/keisuke-miyako/bge-reranker-base-ct2-float16)|`8192`|`768`|`12`|
|[`BAAI/bge-reranker-large`](https://huggingface.co/BAAI/bge-reranker-large)|[`563`](https://huggingface.co/keisuke-miyako/bge-reranker-large-ct2-int8)|[`561`](https://huggingface.co/keisuke-miyako/bge-reranker-large-ct2-int8_float16)|[`1120`](https://huggingface.co/keisuke-miyako/bge-reranker-large-ct2-float16)|`8192`|`1024`|`24`

### Embedding

||`int8`|`int8_float16`|`float16`|`max_position_embeddings`|`hidden_size`|`num_hidden_layers`|`pooling`
|-|-:|-:|-:|-:|-:|-:|-:
|[`sentence-transformers/LaBSE`](https://huggingface.co/sentence-transformers/paraphrase-multilingual-mpnet-base-v2)|[`475`](https://huggingface.co/keisuke-miyako/LaBSE-ct2-int8)|[`474`](https://huggingface.co/keisuke-miyako/LaBSE-ct2-int8_float16)|[`942`](https://huggingface.co/keisuke-miyako/LaBSE-ct2-float16)|`512`|`768`|`12`|`cls`
|[`sentence-transformers/paraphrase-multilingual-mpnet-base-v2`](https://huggingface.co/sentence-transformers/paraphrase-multilingual-mpnet-base-v2)|[`280`](keisuke-miyako/paraphrase-multilingual-mpnet-base-v2-ct2-int8)|[`279`](keisuke-miyako/paraphrase-multilingual-mpnet-base-v2-ct2-int8_float16)|[`555`](keisuke-miyako/paraphrase-multilingual-mpnet-base-v2-ct2-float16)|`512`|`768`|`12`|`mean`
|[`BAAI/bge-small-en-v1.5`](https://huggingface.co/BAAI/bge-small-en-v1.5)|[`34`](https://huggingface.co/keisuke-miyako/bge-small-en-v1.5-ct2-int8)|[`33`](https://huggingface.co/keisuke-miyako/bge-small-en-v1.5-ct2-int8_float16)|[`66`](https://huggingface.co/keisuke-miyako/bge-small-en-v1.5-ct2-float16)|`512`|`384`|`12`|`cls`
|[`BAAI/bge-base-en-v1.5`](https://huggingface.co/BAAI/bge-base-en-v1.5)|[`111`](https://huggingface.co/keisuke-miyako/bge-base-en-v1.5-ct2-int8)|[`110`](https://huggingface.co/keisuke-miyako/bge-base-en-v1.5-ct2-int8_float16)|[`219`](https://huggingface.co/keisuke-miyako/bge-base-en-v1.5-ct2-float16)|`512`|`768`|`12`|`cls`
|[`BAAI/bge-large-en-v1.5`](https://huggingface.co/BAAI/bge-large-en-v1.5)|[`338`](https://huggingface.co/keisuke-miyako/bge-large-en-v1.5-ct2-int8)|[`337`](https://huggingface.co/keisuke-miyako/bge-large-en-v1.5-ct2-int8_float16)|[`670`](https://huggingface.co/keisuke-miyako/bge-large-en-v1.5-ct2-float16)|`512`|`1024`|`24`|`cls`
|[`BAAI/bge-m3`](https://huggingface.co/BAAI/bge-m3)|[`595`](https://huggingface.co/keisuke-miyako/bge-m3-ct2-int8)|[`577`](https://huggingface.co/keisuke-miyako/bge-m3-ct2-int8_float16)|[`1130`](https://huggingface.co/keisuke-miyako/bge-m3-ct2-float16)|`8192`|`1024`|`24`|`cls`
|[`intfloat/e5-small-v2`](https://huggingface.co/intfloat/e5-small-v2)|[`34`](https://huggingface.co/keisuke-miyako/e5-small-v2-ct2-int8)|[`33`](https://huggingface.co/keisuke-miyako/e5-small-v2-ct2-int8_float16)|[`66`](https://huggingface.co/keisuke-miyako/e5-small-v2-ct2-float16)|`512`|`384`|`12`|`mean`
|[`intfloat/e5-base-v2`](https://huggingface.co/intfloat/e5-base-v2)|[`111`](https://huggingface.co/keisuke-miyako/e5-base-v2-ct2-int8)|[`110`](https://huggingface.co/keisuke-miyako/e5-base-v2-ct2-int8_float16)|[`219`](https://huggingface.co/keisuke-miyako/e5-base-v2-ct2-float16)|`512`|`768`|`12`|`mean`
|[`intfloat/e5-large-v2`](https://huggingface.co/intfloat/e5-large-v2)|[`339`](https://huggingface.co/keisuke-miyako/e5-large-v2-ct2-int8)|[`337`](https://huggingface.co/keisuke-miyako/e5-large-v2-ct2-int8_float16)|[`670`](https://huggingface.co/keisuke-miyako/e5-large-v2-ct2-float16)|`512`|`1024`|`24`|`mean`
|[`intfloat/multilingual-e5-small`](https://huggingface.co/intfloat/multilingual-e5-small)|[`120`](https://huggingface.co/keisuke-miyako/multilingual-e5-small-ct2-int8)|[`119`](https://huggingface.co/keisuke-miyako/multilingual-e5-small-ct2-int8_float16)|[`235`](https://huggingface.co/keisuke-miyako/multilingual-e5-small-ct2-float16)|`512`|`384`|`12`|`mean`
|[`intfloat/multilingual-e5-base`](https://huggingface.co/intfloat/multilingual-e5-base)|[`280`](https://huggingface.co/keisuke-miyako/multilingual-e5-base-ct2-int8)|[`279`](https://huggingface.co/keisuke-miyako/multilingual-e5-base-ct2-int8_float16)|[`555`](https://huggingface.co/keisuke-miyako/multilingual-e5-base-ct2-float16)|`512`|`768`|`12`|`mean`
|[`intfloat/multilingual-e5-large`](https://huggingface.co/intfloat/multilingual-e5-large)|[`563`](https://huggingface.co/keisuke-miyako/multilingual-e5-large-ct2-int8)|[`562`](https://huggingface.co/keisuke-miyako/multilingual-e5-large-ct2-int8_float16)|[`1120`](https://huggingface.co/keisuke-miyako/multilingual-e5-large-ct2-float16)|`512`|`1024`|`24`|`mean`
|[`Snowflake/snowflake-arctic-embed-s`](https://huggingface.co/Snowflake/snowflake-arctic-embed-s)|[`34`](https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-s-ct2-int8)|[`33`](https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-s-ct2-int8_float16)|[`66`](https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-s-ct2-float16)|`512`|`384`|`12`|`cls`
|[`Snowflake/snowflake-arctic-embed-l`](https://huggingface.co/Snowflake/snowflake-arctic-embed-l)|[`338`](https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-l-ct2-int8)|[`337`](https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-l-ct2-int8_float16)|[`670`](https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-l-ct2-float16)|`512`|`1024`|`24`|`cls`
|[`sentence-transformers/all-MiniLM-L6-v2`](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2)|[`23`](https://huggingface.co/keisuke-miyako/all-MiniLM-L6-v2-ct2-int8)|[`23`](https://huggingface.co/keisuke-miyako/all-MiniLM-L6-v2-ct2-int8_float16)|[`45`](https://huggingface.co/keisuke-miyako/all-MiniLM-L6-v2-ct2-float16)|`512`|`384`|`6`|`mean`
|[`sentence-transformers/all-MiniLM-L12-v2`](https://huggingface.co/sentence-transformers/all-MiniLM-L12-v2)|[`34`](https://huggingface.co/keisuke-miyako/all-MiniLM-L12-v2-ct2-int8)|[`33`](https://huggingface.co/keisuke-miyako/all-MiniLM-L12-v2-ct2-int8_float16)|[`66`](https://huggingface.co/keisuke-miyako/all-MiniLM-L12-v2-ct2-float16)|`512`|`384`|`12`|`mean`
|[`ibm-granite/granite-embedding-30m-english`](https://huggingface.co/ibm-granite/granite-embedding-30m-english)|[`30`](https://huggingface.co/keisuke-miyako/granite-embedding-30m-english-ct2-int8)|[`30`](https://huggingface.co/keisuke-miyako/granite-embedding-30m-english-ct2-int8_float16)|[`60`](https://huggingface.co/keisuke-miyako/granite-embedding-30m-english-ct2-float16)|`512`|`384`|`6`|`cls`
|[`ibm-granite/granite-embedding-125m-english`](https://huggingface.co/ibm-granite/granite-embedding-125m-english)|[`126`](https://huggingface.co/keisuke-miyako/granite-embedding-125m-english-ct2-int8)|[`126`](https://huggingface.co/keisuke-miyako/granite-embedding-125m-english-ct2-int8_float16)|[`249`](https://huggingface.co/keisuke-miyako/granite-embedding-125m-english-ct2-float16)|`512`|`768`|`12`|`cls`
|[`ibm-granite/granite-embedding-107m-multilingual`](https://huggingface.co/ibm-granite/granite-embedding-107m-multilingual)|[`108`](https://huggingface.co/keisuke-miyako/granite-embedding-107m-multilingual-ct2-int8)|[`108`](https://huggingface.co/keisuke-miyako/granite-embedding-107m-multilingual-ct2-int8_float16)|[`214`](https://huggingface.co/keisuke-miyako/granite-embedding-107m-multilingual-ct2-float16)|`512`|`384`|`6`|`cls`
|[`ibm-granite/granite-embedding-278m-multilingual`](https://huggingface.co/ibm-granite/granite-embedding-278m-multilingual)|[`279`](https://huggingface.co/keisuke-miyako/granite-embedding-278m-multilingual-ct2-int8)|[`279`](https://huggingface.co/keisuke-miyako/granite-embedding-278m-multilingual-ct2-int8_float16)|[`555`](https://huggingface.co/keisuke-miyako/granite-embedding-278m-multilingual-ct2-float16)|`512`|`768`|`12`|`cls`

**CTranslate2** can't use models that depend on external python code for tokenisation:

#### BertJapaneseTokenizer

- ruri-base-v2
- ruri-large-v2

**CTranslate2** does not store or return the dense vector representation of the sentence, which is necessary for using decodes for embeddings. That excludes:

#### Qwen2ForCausalLM

- mixedbread-ai/mxbai-rerank-base-v2
- mixedbread-ai/mxbai-rerank-large-v2
- gte-Qwen2-1.5B-instruct
- gte-Qwen2-7B-instruct

#### LlamaModel

- sarashina-embedding-v1-1b
- sarashina-embedding-v2-1b

As of February 2026, **CTranslate2** does not support several notable model architectures:

#### NomicBertModel

- nomic-embed-text-v1
- nomic-embed-text-v1.5

#### NewModel

- gte-base-en-v1.5
- gte-large-en-v1.5

#### NewForTokenClassification
 
- gte-multilingual-base

#### Gemma3TextModel

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

#### DebertaV2ForSequenceClassification

- mixedbread-ai/mxbai-rerank-base-v1
- mixedbread-ai/mxbai-rerank-large-v1
