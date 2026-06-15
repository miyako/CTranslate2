# _CTranslate2
### Extends `_CLI` to target the `ct2-server` executable.

> _CTranslate2.new (class : 4D.Class)

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| class | 4D.Class | -> | Optional custom controller class (must extend `_CTranslate2_Controller`) |

## Description

`_CTranslate2` extends [`_CLI`](_CLI.md) and passes `"ct2-server"` as the executable name to the parent constructor. It also walks the inheritance chain of the supplied `class` to decide whether to use it as a custom controller or fall back to the default `_CTranslate2_Controller`.

`_CTranslate2` is extended by [`_server`](_server.md) and should not be instantiated directly.

### Properties

In addition to properties inherited from `_CLI`:

| Property | Type | Description |
| --- | --- | --- |
| port | Integer | Port the server is listening on |
| onData | 4D.Function | Forwarded to the controller's `onData` handler |
| onDataError | 4D.Function | Forwarded to the controller's `onDataError` handler |
| onTerminate | 4D.Function | Called by `_CTranslate2_Controller` when the worker terminates |

### Methods

#### bind (option : Object; properties : Collection) → cs.CTranslate2._CLI

Copies listed property names from `option` into `This`. Used to bind event callbacks from an options object before execution.

| Parameter | Type | | Description |
| --- | --- | --- | --- |
| option | Object | -> | Source object |
| properties | Collection | -> | Property names to copy |
| Result | cs.CTranslate2._CLI | <- | `This` |

#### get worker () → 4D.SystemWorker

Returns the active `4D.SystemWorker` from the attached controller.

#### terminate ()

Delegates to `controller.terminate()`, stopping the active worker and draining the command queue.

## See also

- [`_CLI`](_CLI.md) — parent class
- [`_CTranslate2_Controller`](_CTranslate2_Controller.md) — default controller
- [`_server`](_server.md) — extends `_CTranslate2` with `start()`
