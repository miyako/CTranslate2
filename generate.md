# Generate

# T5

```
curl -X POST http://127.0.0.1:8080/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{
  "prompt": [
    "translate English to French: The cat sat on the mat"
  ],
  "max_length": 128,
  "beam_size": 4,
  "stream": true
}'
```
