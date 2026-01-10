# ct2-server

CTranslate2 Inference Engine

```
Usage:  ct2-server -s -e embedding_model -p port 

 -m path     : chat completion model (not implemented)
 -e path     : embedding model (pooling=mean)
 -j          : read chat template from stdin (not implemented)
 -t path     : read chat template from path (not implemented)


 -l          : pooling=last-token (Llama)
 -c          : pooling=cls
 -s          : server (OpenAI compatible endpoint)
 -p          : server listening port (default=8080)
 -h host     : server host (default=127.0.0.1)    
```

## OpenAI Compatible Endpoints

- `/v1/models`
- `/v1/chat/completions` (not implemented)
- `/v1/embeddings`

## Converted CT2 Models

https://huggingface.co/collections/keisuke-miyako/ctranslate2

- https://huggingface.co/keisuke-miyako/bge-small-en-v1.5-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/bge-base-en-v1.5-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/bge-large-en-v1.5-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/bge-m3-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/e5-small-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/e5-base-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/e5-large-v2-ct2-int8_float16
