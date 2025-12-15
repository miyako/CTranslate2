---
layout: default
---

![version](https://img.shields.io/badge/version-21%2B-3B69E9)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/CTranslate2)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/CTranslate2/total)

# Use CTranslate2 from 4D

#### Abstract

[**CTranslate2**](https://github.com/OpenNMT/CTranslate2) is an engine highly optimised for fast local inference, especially **quantised transformer-based models**. Compared to general purpose LLM engines such as llama.cpp, it **uses less memory** and for actual embedding models generates **significantly better results** because it is designed specifically for encoder models whereas GGUF is designed for decoder-only LLM architectures unless manually modified.

In short, if your objective is to build an embedding pipeline for semantic database search, **CTranslate2 is far superior compared to general purpose LLM engines with a llama.cpp based backend** (Ollama, for example).

#### Usage

Instantiate `cs.CTranslate2.CTranslate2` in your *On Startup* database method:

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2

If (False)
    $CTranslate2:=cs.CTranslate2.CTranslate2.new()  //default
Else 
    var $homeFolder : 4D.Folder
    $homeFolder:=Folder(fk home folder).folder(".CTranslate2")
    $folder:=$homeFolder.folder("sentence-transformers/paraphrase-multilingual-mpnet-base-v2")
    var $URL : Text
    $URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/medium.zip"
    var $port : Integer
    $port:=8080
    
    var $event : cs.CTranslate2.CTranslate2Event
    $event:=cs.CTranslate2Event.new()
    /*
        Function onError($params : Object; $error : cs._error)
        Function onSuccess($params : Object)
    */
    $event.onError:=Formula(ALERT($2.message))
    $event.onSuccess:=Formula(ALERT($1.model.name+" loaded!"))
    
    $CTranslate2:=cs.CTranslate2.CTranslate2.new($port; $folder; $URL; {}; $event)
End if 
```

Unless the server is already running (in which case the costructor does nothing), the following procedure runs in the background:

1. The specified model is downloaded via HTTP
2. The `ct2-embedding-cli` program is started in server mode

Now you can test the server:

```
curl -X POST http://127.0.0.1:8080/v1/embeddings \
     -H "Content-Type: application/json" \
     -d '{"input":"雨にも負けず風にも負けず雪にも夏の暑さにも負けぬ丈夫なからだを持ち欲は無く決して瞋からず何時も静かに笑っている"}'
```

You may compare the result with enbeddings generated using a different language:

```
curl -X POST http://127.0.0.1:3000/embeddings \
     -H "Content-Type: application/json" \
     -d '{"input":"Rain won’t stop me. Wind won’t stop me. Neither will driving snow. Sweltering summer heat will only raise my determination. With a body built for endurance, a heart free of greed, I’ll never lose my temper, trying always to keep a quiet smile on my face."}'
```

Or, use AI Kit:

```4d
var $AIClient : cs.AIKit.OpenAI
$AIClient:=cs.AIKit.OpenAI.new()
$AIClient.baseURL:="http://127.0.0.1:3000/v1"

var $text : Text
$text:="The quick brown fox jumps over the lazy dog."

var $responseEmbeddings : cs.AIKit.OpenAIEmbeddingsResult
$responseEmbeddings:=$AIClient.embeddings.create($text)
```

Finally to terminate the server:

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2
$CTranslate2:=cs.CTranslate2.CTranslate2.new()
$CTranslate2.terminate()
```

#### `int8_f16` Converted CT2 models:

|Model|Size|Language| Dimensions|Sequence&nbsp;Length|
|-|-:|:-:|-:|-:|
|[sentence-transformers/all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2)|[`20.5 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/all-MiniLM-L6-v2_int8_float16.zip)|English|`384 `|`512 `
|[sentence-transformers/all-MiniLM-L12-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L12-v2)|[`29.7 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/all-MiniLM-L12-v2_int8_float16.zip)| English|`384`|`512 `
|[intfloat/e5-small-v2](https://huggingface.co/intfloat/e5-small-v2)|[`29.6 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/e5-small-v2_int8_float16.zip)| English|`384 `|`512`
|[intfloat/e5-base-v2](https://huggingface.co/intfloat/e5-base-v2)|[`94.59 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/e5-base-v2_int8_float16.zip)| English|`768 `|`512`
|[intfloat/e5-large-v2](https://huggingface.co/intfloat/e5-large-v2)|[`288.93 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/e5-large-v2_int8_float16.zip)| English|`1024 `|`512`
|[BAAI/bge-small-en-v1.5](https://huggingface.co/BAAI/bge-small-en-v1.5)|[`29.6 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/bge-small-en-v1.5_int8_float16.zip)| English |`384 `|`512 `
[BAAI/bge-base-en-v1.5](https://huggingface.co/BAAI/bge-base-en-v1.5)|[`94.8 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/bge-base-en-v1.5_int8_float16.zip)| English| `768`|`512 `
|[BAAI/bge-large-en-v1.5](https://huggingface.co/BAAI/bge-large-en-v1.5)|[`289.0 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/bge-large-en-v1.5_int8_float16.zip)| English|`1024 `|`512 `
[Snowflake/snowflake-arctic-embed-s](https://huggingface.co/Snowflake/snowflake-arctic-embed-s)|[`29.3 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/snowflake-arctic-embed-s_int8_float16.zip)| English|`384`|`512`
|[intfloat/multilingual-e5-small](https://huggingface.co/intfloat/multilingual-e5-small)|[`109.8 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/multilingual-e5-small_int8_float16.zip)|`94`|`384`|`512`
|[intfloat/multilingual-e5-base](https://huggingface.co/intfloat/multilingual-e5-base)|[`251.5 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/multilingual-e5-base_int8_float16.zip)|`94`|`768`|`512`
|[intfloat/multilingual-e5-large](https://huggingface.co/intfloat/multilingual-e5-large)|[`493.3 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/multilingual-e5-large_int8_float16.zip)|`94 `|`1024 `|`512`
|[BAAI/bge-m3](https://huggingface.co/BAAI/bge-m3)|[`506.9 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/bge-m3_int8_float16.zip)|`100+`|`1024 `|`8192 `
|[Snowflake/snowflake-arctic-embed-l-v2.0](https://huggingface.co/Snowflake/snowflake-arctic-embed-l-v2.0)|[`505.5 MB`](https://github.com/miyako/ct2-embedding-cli/releases/download/models/snowflake-arctic-embed-l-v2.0_int8_float16.zip)|`74`|`1024 `|`8192 `

You can find more models on [Hugging Face](https://huggingface.co). Search specifically for models that are **transformer-based**. Matching model names would typically include tags like:

* e5 ([EmbEddings from bidirectional Encoder representations](https://huggingface.co/intfloat/multilingual-e5-large))
* MiniLM ([Mini Language Model](https://huggingface.co/sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2))
* gte ([General Text Embedding](https://huggingface.co/Alibaba-NLP/gte-multilingual-base))
* bge ([Beijing Academy of AI General Embedding](https://huggingface.co/BAAI/bge-multilingual-gemma2))
* [MPNet](https://huggingface.co/docs/transformers/main/model_doc/mpnet) ([Masked and Permuted Pre-training for Language Understanding](https://huggingface.co/sentence-transformers/paraphrase-multilingual-mpnet-base-v2))

Do **not** choose decoder-only LLMs like LLaMA, GPT, Mistral, or Qwen.

#### Notable Embedding Models not support by CTranslate2:

* Nomic Embed Text v1.x: **NomicBertModel** architecture
* Nomic Embed Text v2: **MoE** (Mixture of Experts) architecture
* Jina: **JinaBERT** architecture
* Instructor:  **T5** architecture
* Qwen2, Mistral, EmbeddingGemma: LLM decoder-based encoder
* [**ModernBERT**](https://huggingface.co/models?other=base_model:finetune:answerdotai/ModernBERT-base) architecture

CTranslate2 relies on mapping standard model architectures like **BERT**, **RoBERTa**, or **DistilBERT** to its C++ inference engine. Some LLMs have moved on from the standard BERT architecture to a custom architecture. 

If a model is not avaiable in `ct2` format, you can use a `python` utility to convert it. See [miyako/ct2-embedding-cli](https://github.com/miyako/ct2-embedding-cli) for details.

#### Discussion

Some developers prefer **CTranslate2** over standard inference engines like **llama.cpp** , **ONNX Runtime**, or **PyTorch** because:

* It is a specialised encoder-only inference engine 
* It has a fast inference routine for CPU  

`float16` quantisation is standard for GPU because it cuts VRAM usage by half without sacrificing much precision on the inference.

Most x86 CPUs do not have native `float16` calculation units. That means the `float16` weights are cast to `float32` for computation which adds overhead. 

CTranslate2 uses optimised instruction sets (AVX2, AVX-512, VNNI) to run operations directly on integers which is drastically faster than `float32` on CPUs.

In addition it has a special quantisation recipe called `int8_float16` where the weights are stored in `int8` (which saves VRAM) but de-quantised on the fly for computation in `float16`.

* `int8`: Fast inference on CPU 
* `float16`: High precision on GPU
* `int8_float16`: Weights are small (Int8) but compute is fast (FP16)
 
#### AI Kit compatibility

The API is compatibile with [Open AI](https://platform.openai.com/docs/api-reference/embeddings). 

|Class|API|Availability|
|-|-|:-:|
|Models|`/v1/models`||
|Chat|`/v1/chat/completions`||
|Images|`/v1/images/generations`||
|Moderations|`/v1/moderations`||
|Embeddings|`/v1/embeddings`|✅|
|Files|`/v1/files`||
