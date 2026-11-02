---
layout: default
title: "Overview"
description: "Local Transformers, no GPU required. Run chat, embeddings, generation, and translation from 4D on Mac and Windows."
---

<section class="hero" aria-labelledby="hero-title">
  <div class="hero-copy">
    <p class="eyebrow"><span class="status-dot" aria-hidden="true"></span> Local inference / Native performance</p>
    <h1 id="hero-title">Local Transformers,<br><span>no GPU required.</span></h1>
    <p class="lede">Chat, embed, generate, and translate. Bring transformer models to your application with a native inference server built for the CPU you already have.</p>
    <div class="actions">
      <a class="button primary" href="{{ '/integration.html' | relative_url }}">Integrate with 4D <span aria-hidden="true">&rarr;</span></a>
      <a class="button secondary" href="#quickstart">Start a local server</a>
    </div>
  </div>
  <div class="engine-panel" aria-label="4D connects over local HTTP to ct2-server, which runs a converted CT2 model on your machine">
    <div class="panel-top"><span class="panel-label">The local stack</span><span class="local-tag">On your machine</span></div>
    <div class="app-node"><span class="node-symbol" aria-hidden="true">4D</span><div><strong>Your application</strong><span>Component, AI Kit or your own HTTP client</span></div></div>
    <div class="connection"><span>HTTP / 127.0.0.1</span></div>
    <div class="engine-node">
      <img src="{{ site.logo | relative_url }}" width="72" height="72" alt="">
      <div><strong>CTranslate2</strong><span>Local inference engine</span></div>
      <span class="engine-badge">SERVER</span>
    </div>
    <div class="model-node"><span class="file-mark" aria-hidden="true">CT2</span><div><strong>A model you choose</strong><span>Converted CT2 model, not GGUF</span></div></div>
    <ul class="endpoint-list" aria-label="Endpoints">
      <li><span>chat</span><code>/v1/chat/completions</code></li>
      <li><span>generate</span><code>/v1/generate</code></li>
      <li><span>embeddings</span><code>/v1/embeddings</code></li>
    </ul>
    <div class="panel-bottom"><span class="status-dot" aria-hidden="true"></span> One server. An API your app can use.</div>
  </div>
</section>

<div class="platform-strip capability-strip" aria-label="Project at a glance">
  <span>macOS <strong>Intel + Apple Silicon</strong></span>
  <span>Windows <strong>x64 + ARM64</strong></span>
  <span>CPU-first <strong>int8 inference</strong></span>
  <span>Open source <strong>MIT license</strong></span>
</div>

<section aria-labelledby="capabilities-title">
  <div class="section-heading">
    <div><p class="eyebrow">One engine, several ways to build</p><h2 id="capabilities-title">Put local models to work.</h2></div>
    <p>Pick the task. Bring a compatible model.<br>Keep inference on your machine.</p>
  </div>
  <div class="capability-grid">
    <a class="capability-card" href="{{ '/chat.html' | relative_url }}"><span class="card-index">01 / Conversation</span><h3>Chat <span aria-hidden="true">&nearr;</span></h3><p>Build text conversations with Qwen 3 and Gemma 3/4, with streaming responses.</p><code>/v1/chat/completions</code></a>
    <a class="capability-card" href="{{ '/embeddings.html' | relative_url }}"><span class="card-index">02 / Semantic search</span><h3>Embed &amp; rerank <span aria-hidden="true">&nearr;</span></h3><p>Turn text into vectors and rank documents with compatible encoder models.</p><code>/v1/embeddings</code></a>
    <a class="capability-card" href="{{ '/generate.html' | relative_url }}"><span class="card-index">03 / Text to text</span><h3>Generate <span aria-hidden="true">&nearr;</span></h3><p>Run encoder-decoder tasks with models such as T5Gemma2 and UMT5.</p><code>/v1/generate</code></a>
    <a class="capability-card" href="{{ '/translate.html' | relative_url }}"><span class="card-index">04 / Across languages</span><h3>Translate <span aria-hidden="true">&nearr;</span></h3><p>Add local machine translation with a converted translation model.</p><code>/v1/translate</code></a>
  </div>
  <p class="section-note">Model support depends on the checkpoint and converter. These server integrations are text-only; see each guide for supported architectures and limitations.</p>
</section>

<section id="quickstart" aria-labelledby="quickstart-title" markdown="1">
<p class="eyebrow">From terminal to first response</p>
## Local starts here.
{: #quickstart-title }

Get a [native server build]({{ '/build.html#platforms-and-triggers' | relative_url }}) for your platform, then prepare a compatible CT2 model using the [chat conversion guide]({{ '/chat.html#model-families' | relative_url }}). Model files are separate from the executable; check the model's license and memory requirements.

<div class="code-heading"><span><b>01</b> Start the server with your converted chat model</span><span>Terminal</span></div>

```sh
./ct2-server -s -g /path/to/ct2-model -h 127.0.0.1 -p 8080
```

On Windows, run `.\ct2-server.exe` with the same options. Keep the process running. In another terminal, wait until `GET /v1/models` succeeds and lists your model before sending a request.

<div class="code-heading"><span><b>02</b> Check the loaded models</span><span>Shell</span></div>

```sh
curl http://127.0.0.1:8080/v1/models
```

<div class="code-heading"><span><b>03</b> Send a message</span><span>POSIX shell</span></div>

```sh
curl http://127.0.0.1:8080/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{
    "messages": [{"role": "user", "content": "Hello from my app."}],
    "max_tokens": 128
  }'
```

The multiline request uses POSIX shell quoting; on Windows, use Git Bash or WSL for this example. For streaming, add `"stream": true` to the JSON and use `curl -N`. Keep the server on `127.0.0.1` unless you have deliberately secured network access.

<a class="text-link" href="{{ '/CLI.html' | relative_url }}">All server options and model references <span aria-hidden="true">&rarr;</span></a>
</section>

<section class="integration-feature" aria-labelledby="integration-title">
  <div>
    <p class="eyebrow">Less plumbing, more application</p>
    <h2 id="integration-title">A native engine.<br>A familiar 4D workflow.</h2>
    <p>The component handles model downloads and starts the server in the background. Use the local HTTP API from your application, or connect through 4D AI Kit for supported endpoints.</p>
    <a class="text-link" href="{{ '/integration.html' | relative_url }}">Explore the component lifecycle <span aria-hidden="true">&rarr;</span></a>
  </div>
  <div class="integration-code" markdown="1">
<div class="code-heading"><span>4D / Component lifecycle</span></div>

```4d
var $engine : cs.CTranslate2.CTranslate2
$engine:=cs.CTranslate2.CTranslate2.new()

// When your application is done
$engine.terminate()
```

<p>See the integration guide for model configuration, events, and AI Kit examples.</p>
  </div>
</section>

<section id="api" aria-labelledby="api-title" markdown="1">
<p class="eyebrow">Connect with tools you know</p>
## A local API for each task.
{: #api-title }

<div class="table-wrap" markdown="1">

| Capability | Endpoint | Models |
| --- | --- | --- |
| **Chat** | `/v1/chat/completions` | Qwen 3, Gemma 3/4 (streaming supported) |
| **Generate** | `/v1/generate` | Encoder-decoder models such as T5Gemma2 and UMT5 |
| **Embeddings & reranking** | `/v1/embeddings`, `/v1/rerank` | Compatible encoder models |
| **Translation** | `/v1/translate` | Converted translation models |

</div>

Model support depends on the checkpoint and converter. See the [chat]({{ '/chat.html#model-families' | relative_url }}), [generation]({{ '/generate.html' | relative_url }}) and [embeddings]({{ '/embeddings.html' | relative_url }}) guides for architectures and limitations.
</section>

<section id="requirements" aria-labelledby="requirements-title" markdown="1">
<p class="eyebrow">Know your runtime</p>
## Built for the machine in front of you.
{: #requirements-title }

The native build pins **CTranslate2 4.8.4**, with Accelerate on macOS and Ruy on Windows. For CPU inference, start with **`int8`** models rather than GPU-oriented `float16` or `int8_float16` weights.

<div class="table-wrap" markdown="1">

| Choose your path | What to know |
| --- | --- |
| **4D component** | 4D 21+. Download the component from [Releases](https://github.com/miyako/CTranslate2/releases) and follow the [integration guide]({{ '/integration.html' | relative_url }}). |
| **Native server** | macOS Intel / Apple Silicon or Windows x64 / ARM64. Obtain platform-specific artifacts from the [native build workflow]({{ '/build.html#platforms-and-triggers' | relative_url }}). |
| **Compatible model** | Use converted CT2 files, not GGUF. Preserve the original Hugging Face configuration as `source_config.json`, separate from CT2's `config.json`. |

</div>

<aside class="callout" markdown="1">
**Native builds and component releases are separate.**

The native workflow produces server and SDK artifacts, not component releases. Previously bundled executables retain their original capabilities until replaced. Use a fresh native build for the model support described in these guides.
</aside>

<a class="text-link" href="{{ '/build.html' | relative_url }}">Builds, signing, and release details <span aria-hidden="true">&rarr;</span></a>
</section>
