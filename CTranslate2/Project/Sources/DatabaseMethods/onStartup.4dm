SET TEXT TO PASTEBOARD:C523("(<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\nYou are a helpful assistant.\n\n# Available Tools\nYou have access to the following functions to help answer the user's request:\n[\n  {\n    \"function\": {\n      \"description\": \"Get the current w"+"eather in a given location\",\n      \"name\": \"get_weather\",\n      \"parameters\": {\n        \"properties\": {\n          \"location\": {\n            \"description\": \"The city and state, e.g. San Francisco, CA\",\n            \"type\": \"string\"\n          }\n        }"+",\n        \"required\": [\n          \"location\"\n        ],\n        \"type\": \"object\"\n      }\n    },\n    \"type\": \"function\"\n  }\n]\n\nIf you need to use a tool, you MUST output ONLY a JSON object wrapped in <tool_call> and </tool_call> tags. Example:\n<tool_ca"+"ll>{\"name\": \"get_weather\", \"arguments\": {\"location\": \"Tokyo\"}}</tool_call><|eot_id|><|start_header_id|>user<|end_header_id|>\n\nWhat is the weather like in Tokyo right now?<|eot_id|><|start_header_id|>assistant<|end_header_id|>\n\n)")

var $CTranslate2 : cs:C1710.CTranslate2

If (False:C215)
	$CTranslate2:=cs:C1710.CTranslate2.new()  //default
Else 
	var $homeFolder : 4D:C1709.Folder
	$homeFolder:=Folder:C1567(fk home folder:K87:24).folder(".CTranslate2")
	var $file : 4D:C1709.File
	var $URL : Text
	var $port : Integer
	
	var $event : cs:C1710.event.event
	$event:=cs:C1710.event.event.new()
/*
Function onError($params : Object; $error : cs.event.error)
Function onSuccess($params : Object; $models : cs.event.models)
Function onData($request : 4D.HTTPRequest; $event : Object)
Function onResponse($request : 4D.HTTPRequest; $event : Object)
Function onTerminate($worker : 4D.SystemWorker; $params : Object)
*/
	
	$event.onError:=Formula:C1597(ALERT:C41($2.message))
	$event.onSuccess:=Formula:C1597(ALERT:C41($2.models.extract("name").join(",")+" loaded!"))
	$event.onData:=Formula:C1597(LOG EVENT:C667(Into 4D debug message:K38:5; This:C1470.file.fullName+":"+String:C10((This:C1470.range.end/This:C1470.range.length)*100; "###.00%")))
	$event.onData:=Formula:C1597(MESSAGE:C88(This:C1470.file.fullName+":"+String:C10((This:C1470.range.end/This:C1470.range.length)*100; "###.00%")))
	$event.onResponse:=Formula:C1597(LOG EVENT:C667(Into 4D debug message:K38:5; This:C1470.file.fullName+":download complete"))
	$event.onResponse:=Formula:C1597(MESSAGE:C88(This:C1470.file.fullName+":download complete"))
	$event.onTerminate:=Formula:C1597(LOG EVENT:C667(Into 4D debug message:K38:5; (["process"; $1.pid; "terminated!"].join(" "))))
	
	$port:=8080
	
	$options:={}
	var $huggingfaces : cs:C1710.event.huggingfaces
	
	$folder:=$homeFolder.folder("multilingual-e5-base")
	$path:="keisuke-miyako/multilingual-e5-base-ct2-int8_float16"
	$URL:="keisuke-miyako/multilingual-e5-base-ct2-int8_float16"
	$embeddings:=cs:C1710.event.huggingface.new($folder; $URL; $path; "embedding")
	
	$folder:=$homeFolder.folder("NMT-EN-FR-CT2")
	$path:="ymoslem/NMT-EN-FR-CT2"
	$URL:="ymoslem/NMT-EN-FR-CT2"
	$translate:=cs:C1710.event.huggingface.new($folder; $URL; $path; "translate"; "source.spm.model")
	
	$folder:=$homeFolder.folder("mmarco-mMiniLMv2-L12-H384-v1")
	$path:="mmarco-mMiniLMv2-L12-H384-v1-ct2-int8_float16"
	$URL:="keisuke-miyako/mmarco-mMiniLMv2-L12-H384-v1-ct2-int8_float16"
	$rerank:=cs:C1710.event.huggingface.new($folder; $URL; $path; "rerank")
	
	$folder:=$homeFolder.folder("Arch-Agent-3B")
	$path:="Arch-Agent-3B-ct2-int8"
	$URL:="keisuke-miyako/Arch-Agent-3B-ct2-int8"
	$chat:=cs:C1710.event.huggingface.new($folder; $URL; $path; "chat")
	
	// $huggingfaces:=cs:C1710.event.huggingfaces.new([$embeddings; $translate; $rerank; $chat])
	$huggingfaces:=cs:C1710.event.huggingfaces.new([$chat])
	$options:={}
	
	$CTranslate2:=cs:C1710.CTranslate2.new($port; $huggingfaces; $homeFolder; $options; $event)
	
End if 