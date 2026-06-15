# CTranslate2
### The public entry point for managing a `ct2-server` instance.

> CTranslate2.new (port : Integer; huggingfaces : cs.event.huggingfaces; HOME : 4D.Folder; options : Object; event : cs.event.event)

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| port | Integer | -> | Port to listen on (default: 8080) |
| huggingfaces | cs.event.huggingfaces | -> | Model download parameters |
| HOME | 4D.Folder | -> | Home folder (default: `Folder(fk home folder).folder(".CTranslate2")`) |
| options | Object | -> | Command-line options passed to `ct2-server` |
| event | cs.event.event | -> | Callback functions (onError, onSuccess, onData, onResponse, onTerminate) |

## Description

`cs.CTranslate2.CTranslate2` is the main class you instantiate to manage a `ct2-server` process. It extends `_interface` and orchestrates the full lifecycle: checking whether a server is already running on the given port, downloading model files from Hugging Face if needed, and starting the server process in the background via a worker.

If a server is already running on the specified `port`, the constructor exits immediately without starting a new one.

Parameter defaults applied by the constructor:

- `port`: defaults to `8080` if `0`, negative, or greater than `65535`
- `HOME`: defaults to `Folder(fk home folder).folder(".CTranslate2")` if not provided or non-existent
- `huggingfaces`: defaults to `keisuke-miyako/all-MiniLM-L6-v2-ct2-int8_float16` (embedding) if `Null` or empty

### options properties

| Property | Type | CLI flag | Description |
| --- | --- | --- | --- |
| embeddings_model | 4D.Folder | `-e` | Path to a CTranslate2 embedding model directory |
| rerank_model | 4D.Folder | `-r` | Path to a CTranslate2 reranking model directory |
| chat_model | 4D.Folder | `-g` | Path to a CTranslate2 chat/generation model directory |
| translate_model | 4D.Folder | `-m` | Path to a CTranslate2 translation model directory |
| generate_model | 4D.Folder | `-a` | Path to a CTranslate2 text generation model directory |
| port | Integer | `-p` | Port to listen on |
| host | Text | `-h` | Host address to bind |
| chat_template | Text | `-t` | Chat template string |
| pooling | Text | `-c` / `-l` | Pooling strategy: `"cls"` → `-c`, `"last-token"` → `-l`, `"mean"` (default, no flag) |
| HF_TOKEN | Text | _(auth header)_ | Hugging Face access token for gated models |

Additional options with underscore-separated names (e.g. `max_length`) are passed through as `--max-length value`. Boolean `true` values produce flags without a value.

### Domain mapping

Each `cs.event.huggingface` descriptor has a `domain` property that determines which option key the downloaded model folder is assigned to:

| domain | options key |
| --- | --- |
| `"embedding"` | `embeddings_model` |
| `"translate"` | `translate_model` |
| `"generate"` | `generate_model` |
| `"rerank"` | `rerank_model` |
| `"chat"` | `chat_model` |

### API compatibility

| Endpoint | Availability |
| --- | --- |
| `/v1/embeddings` | ✅ |
| `/v1/rerank` | ✅ |
| `/v1/chat/completions` | ✅ |
| `/v1/models` | ✅ |

## Examples

### Minimal (defaults)

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2
$CTranslate2:=cs.CTranslate2.CTranslate2.new()
```

### Embedding model

```4d
var $homeFolder : 4D.Folder
$homeFolder:=Folder(fk home folder).folder(".CTranslate2")

var $event : cs.event.event
$event:=cs.event.event.new()
$event.onError:=Formula(ALERT($2.message))
$event.onSuccess:=Formula(ALERT($2.models.extract("name").join(",")+" loaded!"))
$event.onData:=Formula(MESSAGE(This.file.fullName+":"+String((This.range.end/This.range.length)*100; "###.00%")))
$event.onResponse:=Formula(LOG EVENT(Into 4D debug message; This.file.fullName+":download complete"))
$event.onTerminate:=Formula(LOG EVENT(Into 4D debug message; (["process"; $1.pid; "terminated!"].join(" "))))

var $folder : 4D.Folder
$folder:=$homeFolder.folder("multilingual-e5-base-ct2-int8_float16")

var $embeddings : cs.event.huggingface
$embeddings:=cs.event.huggingface.new(\
    $folder; \
    "keisuke-miyako/multilingual-e5-base-ct2-int8_float16"; \
    "keisuke-miyako/multilingual-e5-base-ct2-int8_float16"; \
    "embedding")

var $huggingfaces : cs.event.huggingfaces
$huggingfaces:=cs.event.huggingfaces.new([$embeddings])

var $CTranslate2 : cs.CTranslate2.CTranslate2
$CTranslate2:=cs.CTranslate2.CTranslate2.new(8080; $huggingfaces; $homeFolder; {}; $event)
```

### Test the embedding endpoint

```
curl -X POST http://127.0.0.1:8080/v1/embeddings \
     -H "Content-Type: application/json" \
     -d '{"input":"The quick brown fox jumps over the lazy dog."}'
```

### Test the rerank endpoint

```
curl --request POST \
  --url http://127.0.0.1:8080/v1/rerank \
  --header 'Content-Type: application/json' \
  --data '{
    "model": "rerank-english-v3.0",
    "query": "What is the capital of the United States?",
    "top_n": 3,
    "documents": [
      "Carson City is the capital city of the American state of Nevada.",
      "Washington, D.C. is the capital of the United States.",
      "Capital punishment has existed in the United States since before it was a country."
    ]
  }'
```

### Use with AI Kit

```4d
var $AIClient : cs.AIKit.OpenAI
$AIClient:=cs.AIKit.OpenAI.new()
$AIClient.baseURL:="http://127.0.0.1:8080/v1"

var $text : Text
$text:="The quick brown fox jumps over the lazy dog."

var $responseEmbeddings : cs.AIKit.OpenAIEmbeddingsResult
$responseEmbeddings:=$AIClient.embeddings.create($text)
```

### Terminate the server

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2
$CTranslate2:=cs.CTranslate2.CTranslate2.new()
$CTranslate2.terminate()
```

## See also

- [`_interface`](_interface.md) — parent class providing `terminate()` and TCP-check helpers
- [`_models`](_models.md) — download and model lifecycle management
- [`_Model`](_Model.md) — concrete model subclass
- [`_server`](_server.md) — CLI wrapper for `ct2-server`
