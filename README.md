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
