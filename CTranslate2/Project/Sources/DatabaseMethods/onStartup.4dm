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
	
	$folder:=$homeFolder.folder("ELYZA-japanese-Llama-2-7b-fast-instruct")
	$path:="ELYZA-japanese-Llama-2-7b-fast-instruct-ct2-int8"
	$URL:="keisuke-miyako/ELYZA-japanese-Llama-2-7b-fast-instruct-ct2-int8"
	$chat:=cs:C1710.event.huggingface.new($folder; $URL; $path; "chat")
	
	$huggingfaces:=cs:C1710.event.huggingfaces.new([$embeddings; $translate; $rerank; $chat])
	$options:={}
	
	//$CTranslate2:=cs:C1710.CTranslate2.new($port; $huggingfaces; $homeFolder; $options; $event)
	
End if 