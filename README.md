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
- https://huggingface.co/keisuke-miyako/multilingual-e5-small-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/multilingual-e5-base-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/multilingual-e5-large-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-s-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/snowflake-arctic-embed-l-v2.0-ct2-int8_float16
- ~~nomic-embed-text-v1~~
- ~~nomic-embed-text-v1.5~~
- https://huggingface.co/keisuke-miyako/all-MiniLM-L6-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/all-MiniLM-L12-v2-ct2-int8_float16
- ~~embeddinggemma-300m~~
- ~~amber-base~~
- ~~amber-large~~
- ~~gte-base-en-v1.5~~
- ~~gte-large-en-v1.5~~
- ~~gte-multilingual-base~~
- ~~gte-modernbert-base~~
- ~~gte-Qwen2-1.5B-instruct~~
- ~~gte-Qwen2-7B-instruct~~
- ~~universal-sentence-encoder~~
- ~~universal-sentence-encoder-large~~
- ~~universal-sentence-encoder-multilingual~~
- ~~universal-sentence-encoder-multilingual-large~~
- ~~granite-embedding-small-english-r2~~
- ~~granite-embedding-english-r2~~
- https://huggingface.co/keisuke-miyako/granite-embedding-30m-english-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/granite-embedding-125m-english-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/granite-embedding-107m-multilingual-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/granite-embedding-278m-multilingual-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/sarashina-embedding-v1-1b-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/sarashina-embedding-v2-1b-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/ruri-base-v2-ct2-int8_float16
- https://huggingface.co/keisuke-miyako/ruri-large-v2-ct2-int8_float16
- ~~ruri-v3-30m~~
- ~~ruri-v3-70m~~
- ~~ruri-v3-130m~~
- ~~ruri-v3-310m~~
