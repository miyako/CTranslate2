# Translate

||`max_position_embeddings`|`max_length`
|-|-:|-:|
|opus-mt-en-fr|`512`|`512`
|opus-mt-fr-en|`512`|`512`
|opus-mt-tc-big-en-fr|`1024`|`512`
|opus-mt-tc-big-fr-en|`1024`|`512`
|nllb-200-distilled-600M|`1024`|`200`|
|nllb-200-distilled-1.3B|`1024`|`200`|
|mbart-large-50-many-to-many-mmt|`1024`|`200`
|mbart-large-50-many-to-one-mmt|`1024`|`200`
|mbart-large-50-one-to-many-mmt|`1024`|`200`

# NLLB

```
curl -X POST http://127.0.0.1:8080/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{
  "input": [
    "The weather is beautiful today.",
    "The weather is very bad today." 
  ],
  "max_length": 128,
  "beam_size": 4,
  "from": "eng_Latn",
  "to": "fra_Latn",
  "stream": false
}'
```

# one to many mBART

```
curl -X POST http://127.0.0.1:8080/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{
  "input": [
    "The weather is beautiful today.",
    "The weather is very bad today." 
  ],
  "max_length": 128,
  "beam_size": 4,
  "to": "fr_XX",
  "stream": false
}'
```

# many to one mBART

```
curl -X POST http://127.0.0.1:8080/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{
  "input": [
    "Le temps est beau aujourd\u0027hui.",
    "Le temps est tr\u00e8s mauvais aujourd\u0027hui."
  ],
  "max_length": 128,
  "beam_size": 4,
  "from": "fr_XX",
  "to": "en_XX",
  "stream": false
}'
```
