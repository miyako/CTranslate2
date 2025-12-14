var $CTranslate2 : cs:C1710.CTranslate2

If (False:C215)
	$CTranslate2:=cs:C1710.CTranslate2.new()  //default
Else 
	var $homeFolder : 4D:C1709.Folder
	$homeFolder:=Folder:C1567(fk home folder:K87:24).folder(".CTranslate2")
	
	Case of 
		: (True:C214)
			$folder:=$homeFolder.folder("bge/bge-base-en-v1.5-f16")
			$URL:="https://huggingface.co/CompendiumLabs/bge-base-en-v1.5-gguf/resolve/main/bge-base-en-v1.5-f16.gguf"
		: (False:C215)
			$folder:=$homeFolder.folder("sentence-transformers/all-MiniLM-L6-v2_f16")
			$URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/all-MiniLM-L6-v2_f16.zip"
		: (False:C215)
			$folder:=$homeFolder.folder("sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2_f16")
			$URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/paraphrase-multilingual-MiniLM-L12-v2_f16.zip"
	End case 
	
	var $port : Integer
	$port:=8080
	
	var $event : cs:C1710.CTranslate2Event
	$event:=cs:C1710.CTranslate2Event.new()
/*
Function onError($params : Object; $error : cs._error)
Function onSuccess($params : Object)
*/
	$event.onError:=Formula:C1597(ALERT:C41($2.message))
	$event.onSuccess:=Formula:C1597(ALERT:C41($1.model.name+" loaded!"))
	
	$CTranslate2:=cs:C1710.CTranslate2.new($port; $folder; $URL; {}; $event)
End if 