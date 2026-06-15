# _server
### Extends `_CTranslate2` to build and launch the `ct2-server` command line.

> _server.new (controller : 4D.Class)

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| controller | 4D.Class | -> | Optional custom controller class |

## Description

`_server` extends [`_CTranslate2`](_CTranslate2.md) and provides the `start` method, which assembles the full `ct2-server` command string from an options object and launches it via `_CLI_Controller.execute`.

`_server` is never instantiated directly by application code. It is managed internally by the worker infrastructure; `cs.CTranslate2.CTranslate2` delegates to it via `cs.CTranslate2.workers.worker`.

### Methods

#### start (option : Object) → 4D.SystemWorker

Builds the CLI command and starts the server.

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| option | Object | -> | Server options (see [CTranslate2](CTranslate2.md) for the full property list) |
| Result | 4D.SystemWorker | <- | The launched worker |

**Argument construction rules:**

The command always begins with `ct2-server -s` (server mode). Named model paths are then appended using short flags:

| Option key | Flag | Condition |
| --- | --- | --- |
| `embeddings_model` | `-e` | Folder exists |
| `rerank_model` | `-r` | Folder exists |
| `chat_model` | `-g` | Folder exists |
| `translate_model` | `-m` | Folder exists |
| `generate_model` | `-a` | Folder exists |
| `port` | `-p` | Non-zero Integer |
| `host` | `-h` | Non-empty Text |
| `chat_template` | `-t` | Non-empty Text |
| `pooling: "cls"` | `-c` | — |
| `pooling: "last-token"` | `-l` | — |
| `pooling: "mean"` | _(no flag)_ | Default |

After the named options, remaining keys in `option` are emitted as long-form flags. Keys are transformed with underscores replaced by hyphens. The following keys are reserved and excluded from the general loop: `i`, `o`, `s`, `j`, `c`, `l`, `_`, `h`, `e`, `embeddings_model`, `r`, `rerank_model`, `g`, `chat_model`, `a`, `generate_model`, `t`, `chat_template`, `p`, `port`, `host`, `pooling`, `m`, `translate_model`, `HF_TOKEN`.

| Value type | CLI form |
| --- | --- |
| Real / Integer | `--flag value` |
| Text | `--flag escaped-value` |
| Boolean `True` | `--flag` (no value) |
| 4D.File (exists) | `--flag escaped-path` |

## Examples

`_server` is used indirectly via `cs.CTranslate2.CTranslate2`:

```4d
var $CTranslate2 : cs.CTranslate2.CTranslate2
$CTranslate2:=cs.CTranslate2.CTranslate2.new(8080; $huggingfaces; $homeFolder; $options; $event)
```

To terminate:

```4d
$CTranslate2:=cs.CTranslate2.CTranslate2.new()
$CTranslate2.terminate()
```

## See also

- [`_CTranslate2`](_CTranslate2.md) — parent class
- [`_CLI_Controller`](_CLI_Controller.md) — executes the assembled command
- [`_Model`](_Model.md) — calls `_server.start` after model download completes
