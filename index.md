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

Instantiate `cs.CTranslate2.CTranslate2`  in your *On Startup* database method:

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2

If (True)
    $CTranslate2:=cs.CTranslate2.CTranslate2.new()  //default
Else 
    var $modelsFolder : 4D.Folder
    $modelsFolder:=Folder(fk home folder).folder(".CTranslate2")
    $folder:=$modelsFolder.folder("sentence-transformers/paraphrase-multilingual-mpnet-base-v2")
    var $URL : Integer
    $URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/medium.zip"
    var $port : Integer
    $port:=8080
    $CTranslate2:=cs.CTranslate2.CTranslate2.new($port; $folder; $URL)
End if 
```

Unless the server is alraedy running (in which case the costructor does nothing), the following procedure runs in the background:

1. The specified model is download via HTTP
2. The `ct2-embedding-cli` program is started in server mode

Now you can test the server:

```
curl -X POST http://127.0.0.1:3000/embeddings \
     -H "Content-Type: application/json" \
     -d '{"input":"雨にも負けず風にも負けず雪にも夏の暑さにも負けぬ丈夫なからだを持ち欲は無く決して瞋からず何時も静かに笑っている"}'
```

You may compare the result with enbeddings generated using a different language

```
curl -X POST http://127.0.0.1:3000/embeddings \
     -H "Content-Type: application/json" \
     -d '{"input":"Rain won’t stop me. Wind won’t stop me. Neither will driving snow. Sweltering summer heat will only raise my determination. With a body built for endurance, a heart free of greed, I’ll never lose my temper, trying always to keep a quiet smile on my face."}'
```

Finally to terminate the server

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2
$CTranslate2:=cs.CTranslate2.CTranslate2.new()
$CTranslate2.terminate()
```

#### AI Kit compatibility

The API is compatibile with [Open AI](https://platform.openai.com/docs/api-reference/embeddings). 

#### Models

For testing I have uploaded ct2 models in 3 difference sizes.

|Scale|Dimesnions|Model|Size on Disk|
|-|-:|-|-:|
|Large|`1024`|[intfloat/multilingual-e5-large](https://huggingface.co/intfloat/multilingual-e5-large)|`587.1 MB`|
|Medium|`768`|[sentence-transformers/paraphrase-multilingual-mpnet-base-v2](https://huggingface.co/sentence-transformers/paraphrase-multilingual-mpnet-base-v2)|`296.2 MB`| 
|Small|`384`|[sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2](https://huggingface.co/sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2)|`135.4 MB`| 

You can find more models on [Hugging Face](https://huggingface.co). Search specifically for models that are **transformer-based**. Matching model names would typically include tags like:

* e5 ([EmbEddings from bidirectional Encoder representations](https://huggingface.co/intfloat/multilingual-e5-large))
* MiniLM ([Mini Language Model](https://huggingface.co/sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2))
* gte ([General Text Embedding](https://huggingface.co/Alibaba-NLP/gte-multilingual-base))
* bge ([Beijing Academy of AI General Embedding](https://huggingface.co/BAAI/bge-multilingual-gemma2))
* [MPNet](https://huggingface.co/docs/transformers/main/model_doc/mpnet) ([Masked and Permuted Pre-training for Language Understanding](https://huggingface.co/sentence-transformers/paraphrase-multilingual-mpnet-base-v2))

Do **not** choose decoder-only LLMs like LLaMA, GPT, Mistral, or Qwen.

Repositories already conveted to `ct2` would include files like

* config.json
* tokenizer_config.json
* tokenizer.json

If a model is not avaiable in `ct2` format, you can use a `python` utility to convert it. See [miyako/ct2-embedding-cli](https://github.com/miyako/ct2-embedding-cli) for details.

