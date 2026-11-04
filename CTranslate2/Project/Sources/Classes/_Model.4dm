property embeddings_model : 4D:C1709.Folder
property translate_model : 4D:C1709.Folder
property generate_model : 4D:C1709.Folder
property rerank_model : 4D:C1709.Folder
property chat_model : 4D:C1709.Folder
property _started : Boolean

Class extends _models

Class constructor($port : Integer; $huggingfaces : cs:C1710.event.huggingfaces; $options : Object; $formula : 4D:C1709.Function; $event : cs:C1710.event.event)
	
	Super:C1705($port; $huggingfaces; $options; $formula; $event)
	
	If (Not:C34(This:C1470.offline))
		This:C1470.download()
	End if 
	
Function models() : cs:C1710.event.models
	
	var $models : Collection
	$models:=[]
	
	var $_model : Text
	For each ($_model; This:C1470._models)
		$models.push(cs:C1710.event.model.new($_model; True:C214))
	End for each 
	
	return cs:C1710.event.models.new($models)
	
Function onDownload($oid : Text)
	
	var $downloaded : cs:C1710.event.huggingface
	$downloaded:=This:C1470.files.query("oid == :1"; $oid).first()
	
	If ($downloaded#Null:C1517)
		var $model : Object
		$model:=OB Instance of:C1731($downloaded.folder; 4D:C1709.Folder)\
			 ? $downloaded.folder.folder($downloaded.path).parent : $downloaded.folder
		
		Case of 
			: ($downloaded.domain="embedding") && (This:C1470.options.embeddings_model=Null:C1517)
				This:C1470.options.embeddings_model:=$model
			: ($downloaded.domain="translate") && (This:C1470.options.translate_model=Null:C1517)
				This:C1470.options.translate_model:=$model
			: ($downloaded.domain="generate") && (This:C1470.options.generate_model=Null:C1517)
				This:C1470.options.generate_model:=$model
			: ($downloaded.domain="rerank") && (This:C1470.options.rerank_model=Null:C1517)
				This:C1470.options.rerank_model:=$model
			: ($downloaded.domain="chat") && (This:C1470.options.chat_model=Null:C1517)
				This:C1470.options.chat_model:=$model
		End case 
	End if 
	
	Super:C1706.onDownload($oid)
	
Function _onNothingToDownload()
	
	var $keys : Collection
	$keys:=["embeddings_model"; "translate_model"; "generate_model"; "rerank_model"; "chat_model"]
	var $paths : Collection
	$paths:=[]
	var $key : Text
	For each ($key; $keys)
		If (OB Instance of:C1731(This:C1470.options[$key]; 4D:C1709.Folder))
			If (This:C1470.options[$key].exists)
				This:C1470.start()
				return 
			End if 
		End if 
		If (Value type:C1509(This:C1470.options[$key])=Is object:K8:27) && (Value type:C1509(This:C1470.options[$key].path)=Is text:K8:3)
			$paths.push(This:C1470.options[$key].path)
		End if 
	End for each 
	var $message : Text
	$message:="no model to load: nothing to download and no model option is an existing folder"
	If ($paths.length#0)
		$message+=": "+$paths.join(", ")
	End if 
	LOG EVENT:C667(Into 4D debug message:K38:5; "CTranslate2: "+$message)
	This:C1470._notifyError(cs:C1710.event.error.new(2; $message; This:C1470.models()))
	
Function start()
	
	If (This:C1470._started)
		return 
	End if 
	This:C1470._started:=True:C214
	var $CTranslate2 : cs:C1710.workers.worker
	$CTranslate2:=cs:C1710.workers.worker.new(cs:C1710._server)
	$CTranslate2.start(This:C1470.options.port; This:C1470.options)
	
	If (This:C1470.event#Null:C1517) && (OB Instance of:C1731(This:C1470.event; cs:C1710.event.event))
		This:C1470.event.onSuccess.call(This:C1470; This:C1470.options; This:C1470.models())
	End if 