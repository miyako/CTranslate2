# _CTranslate2_Controller
### Extends `_CLI_Controller` with `onTerminate` forwarding for the `ct2-server` process.

> _CTranslate2_Controller.new (CLI : cs.CTranslate2._CLI)

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| CLI | cs.CTranslate2._CLI | -> | The owning `_CLI` instance |

## Description

`_CTranslate2_Controller` is the default controller used by [`_CTranslate2`](_CTranslate2.md). It inherits all command-queue and worker-management behaviour from [`_CLI_Controller`](_CLI_Controller.md).

Unlike the controllers in other namespaces (e.g. `cs.curl`, `cs.xls_rs`), `_CTranslate2_Controller` does **not** override `onData`, `onDataError`, `onResponse`, or `onError` — those callbacks remain as the inherited no-op `_onEvent` handler. Only `onTerminate` is overridden, forwarding the termination event to the owning `_server` instance's `onTerminate` callback.

This reflects the nature of `ct2-server`: it is a long-running HTTP server process rather than a command that produces incremental stdout output. stdout/stderr are kept quiet on purpose, because a long-running server can produce a large amount of output; `event.onStdOut`/`onStdErr` are copied into the server options but are not forwarded.

`cs.CTranslate2.CTranslate2.new()` does not accept a controller and always uses `_CTranslate2_Controller`. A custom subclass is used only when you pass its class to `_server` yourself (for example `cs.CTranslate2._server.new(cs.MyController)`), which bypasses the port check and model download.

### Overridden event callbacks

#### onTerminate ($worker : 4D.SystemWorker; $params : Object)

Called when the managed `SystemWorker` terminates. Looks up `onTerminate` on the owning `_server` instance and calls it if present.

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| $worker | 4D.SystemWorker | -> | The worker that terminated |
| $params | Object | -> | Termination parameters from the system worker |

## See also

- [`_CLI_Controller`](_CLI_Controller.md) — parent class
- [`_CTranslate2`](_CTranslate2.md) — attaches this controller by default
- [`_server`](_server.md) — the `_CTranslate2` subclass whose `onTerminate` is forwarded here
