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
