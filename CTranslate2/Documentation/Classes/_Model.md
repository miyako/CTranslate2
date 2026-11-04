# _Model
### Extends `_models` to assign model folder paths by domain and launch `ct2-server` after download.

> _Model.new (port : Integer; huggingfaces : cs.event.huggingfaces; options : Object; formula : 4D.Function; event : cs.event.event)

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| port | Integer | -> | Port to listen on |
| huggingfaces | cs.event.huggingfaces | -> | Model download parameters |
| options | Object | -> | Command-line options (mutated; model folder keys are set as downloads complete) |
| formula | 4D.Function | -> | Internal response callback |
| event | cs.event.event | -> | Callback functions, or `Null` for no callbacks |

## Description

`_Model` is the concrete implementation of [`_models`](_models.md). After calling `Super` it immediately triggers `download()` unless `offline` is `true`.

It overrides these methods from `_models`:

### models () → cs.event.models

Returns a `cs.event.models` collection built from the internal `_models` list (repository identifiers of the form `user/repo`). Each entry is wrapped as a `cs.event.model` with `isHuggingFace: True`.

### onDownload (oid : Text)

Overrides the virtual base. When a file completes downloading, resolves the parent folder of that file and assigns it to the appropriate `options` key based on the `domain` of the descriptor — but only if that key is not already set (first-write wins):

| domain | options key set |
| --- | --- |
| `"embedding"` | `options.embeddings_model` |
| `"translate"` | `options.translate_model` |
| `"generate"` | `options.generate_model` |
| `"rerank"` | `options.rerank_model` |
| `"chat"` | `options.chat_model` |

Then calls `Super.onDownload($oid)` to remove the entry from `files` and trigger `start()` when the queue empties.

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| oid | Text | -> | OID of the completed download |

### _onNothingToDownload ()

Called by `download()` when no file needs downloading. Calls `start()` when one of `embeddings_model`, `translate_model`, `generate_model`, `rerank_model` or `chat_model` in `options` is an existing `4D.Folder` (the only form `_server` passes to `ct2-server`). Otherwise calls `event.onError` with code `2`, naming any model path that was given, and does not launch.

### start ()

Overrides the virtual base. Launches at most once per instance; later calls (for example a duplicate download completion for files that share a Git OID) are ignored. Creates a `cs.CTranslate2.workers.worker` wrapping `_server`, calls `worker.start(port, options)`, then fires `event.onSuccess` with the current options and model list. `onSuccess` means the launch was requested, not that the server is ready to serve requests.

### Properties

In addition to properties inherited from `_models`:

| Property | Type | Description |
| --- | --- | --- |
| embeddings_model | 4D.Folder | Set after the first `embedding`-domain download completes |
| translate_model | 4D.Folder | Set after the first `translate`-domain download completes |
| generate_model | 4D.Folder | Set after the first `generate`-domain download completes |
| rerank_model | 4D.Folder | Set after the first `rerank`-domain download completes |
| chat_model | 4D.Folder | Set after the first `chat`-domain download completes |

## See also

- [`_models`](_models.md) — parent class
- [`_server`](_server.md) — launched by `start()`
- [`CTranslate2`](CTranslate2.md) — public entry point
