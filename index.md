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

#### Quantisation

The `int8_float16` format is primarily designed for **NVIDIA GPUs**. It stores weights in 8-bit integers but converts them to 16-bit floating point for maximum efficiency (storage+speed). CTranslate2 falls back to `float32` if CUDA is unavailable, which defeats the purpose of this hybrid format. 

The `float16` format is also designed for GPUs that support native 16-bit maths. The CPU backend of **CTranslate2** usually performs calculations in `float32` even on a CPU like Apple Silicon that actually has native 16-bit maths. The weights are automatically converted to 32-bit at startup.

The `int8` format takes advantage of `NEON` instructions on Apple Silicon and `AVX2` `AVX-512` `VNNI` instructions on Intel or AMD to **accelerate maths**. **You should always use the `int8` format on a PC or Mac with no GPU**.

#### Usage

Instantiate `cs.CTranslate2.CTranslate2` in your *On Startup* database method:

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2

If (False)
    $CTranslate2:=cs.CTranslate2.CTranslate2.new()  //default
Else 
    var $homeFolder : 4D.Folder
    $homeFolder:=Folder(fk home folder).folder(".CTranslate2")
    var $file : 4D.File
    var $URL : Text
    var $port : Integer
    
    var $event : cs.event.event
    $event:=cs.event.event.new()
    /*
        Function onError($params : Object; $error : cs.event.error)
        Function onSuccess($params : Object; $models : cs.event.models)
        Function onData($request : 4D.HTTPRequest; $event : Object)
        Function onResponse($request : 4D.HTTPRequest; $event : Object)
        Function onTerminate($worker : 4D.SystemWorker; $params : Object)
    */
    
    $event.onError:=Formula(ALERT($2.message))
    $event.onSuccess:=Formula(ALERT($2.models.extract("name").join(",")+" loaded!"))
    $event.onData:=Formula(LOG EVENT(Into 4D debug message; This.file.fullName+":"+String((This.range.end/This.range.length)*100; "###.00%")))
    $event.onData:=Formula(MESSAGE(This.file.fullName+":"+String((This.range.end/This.range.length)*100; "###.00%")))
    $event.onResponse:=Formula(LOG EVENT(Into 4D debug message; This.file.fullName+":download complete"))
    $event.onResponse:=Formula(MESSAGE(This.file.fullName+":download complete"))
    $event.onTerminate:=Formula(LOG EVENT(Into 4D debug message; (["process"; $1.pid; "terminated!"].join(" "))))
    
    $port:=8080
    
    $options:={}
    var $huggingfaces : cs.event.huggingfaces
    
    $folder:=$homeFolder.folder("multilingual-e5-base-ct2-int8_float16")
    $path:="keisuke-miyako/multilingual-e5-base-ct2-int8_float16"
    $URL:="keisuke-miyako/multilingual-e5-base-ct2-int8_float16"
    $embeddings:=cs.event.huggingface.new($folder; $URL; $path; "embedding")
    
    $huggingfaces:=cs.event.huggingfaces.new([$embeddings])
    $options:={}
    
    $CTranslate2:=cs.CTranslate2.CTranslate2.new($port; $huggingfaces; $homeFolder; $options; $event)
    
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
curl -X POST http://127.0.0.1:8080/v1/embeddings \
     -H "Content-Type: application/json" \
     -d '{"input":"Rain won’t stop me. Wind won’t stop me. Neither will driving snow. Sweltering summer heat<2028>will only raise my determination. With a body built for endurance, a heart free of greed, I’ll never lose my temper, trying always to keep a quiet smile on my face."}'
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

CTranslate2 relies on mapping standard model architectures like **BERT**, **RoBERTa**, or **DistilBERT** to its C++ inference engine. Some LLMs have moved on from the standard BERT architecture to a custom architecture. 

If a model is not avaiable in `ct2` format, you can use a `python` utility to convert it. See [miyako/ct2-embedding-cli](https://github.com/miyako/ct2-embedding-cli) for details.

#### Discussion

Some developers prefer **CTranslate2** over standard inference engines like **llama.cpp** , **ONNX Runtime**, or **PyTorch** because:

* It is a specialised encoder-only inference engine 
* It has a fast inference routine for CPU  

Most x86 CPUs do not have native `float16` calculation units. That means the `float16` weights are cast to `float32` for computation which adds overhead. CTranslate2 uses optimised instruction sets (AVX2, AVX-512, VNNI) to run operations directly on integers which is drastically faster than `float32` on CPUs.
 
#### AI Kit compatibility

The API is compatibile with [Open AI](https://platform.openai.com/docs/api-reference/embeddings). 

|Class|API|Availability|
|-|-|:-:|
|Models|`/v1/models`|✅|
|Chat|`/v1/chat/completions`||
|Images|`/v1/images/generations`||
|Moderations|`/v1/moderations`||
|Embeddings|`/v1/embeddings`|✅|
|Files|`/v1/files`||
