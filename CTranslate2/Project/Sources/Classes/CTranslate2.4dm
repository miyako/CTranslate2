Class constructor($port : Integer; $folder : 4D:C1709.Folder; $URL : Text)
	
	var $CTranslate2 : cs:C1710._worker
	$CTranslate2:=cs:C1710._worker.new()
	
	If (Not:C34($CTranslate2.isRunning()))
		
		If (Value type:C1509($folder)#Is object:K8:27) || (Not:C34(OB Instance of:C1731($folder; 4D:C1709.Folder))) || ($URL="")
			var $modelsFolder : 4D:C1709.Folder
			$modelsFolder:=Folder:C1567(fk home folder:K87:24).folder(".CTranslate2")
			$folder:=$modelsFolder.folder("sentence-transformers/paraphrase-multilingual-mpnet-base-v2")
			$URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/medium.zip"
		End if 
		
		If ($port=0) || ($port<0) || ($port>65535)
			$port:=3000
		End if 
		
		CALL WORKER:C1389("Ctranslate2_Start"; This:C1470._Start; $port; $folder; $URL)
		
	End if 
	
Function _Start($port : Integer; $folder : 4D:C1709.Folder; $URL : Text)
	
	var $model : cs:C1710.Model
	$model:=cs:C1710.Model.new($port; $folder; $URL)
	
Function terminate()
	
	var $CTranslate2 : cs:C1710._worker
	$CTranslate2:=cs:C1710._worker.new()
	$CTranslate2.terminate()