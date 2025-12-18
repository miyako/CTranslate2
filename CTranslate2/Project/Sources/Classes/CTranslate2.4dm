Class constructor($port : Integer; $folder : 4D:C1709.Folder; $URL : Text; $options : Object; $event : cs:C1710.event.event)
	
	var $CTranslate2 : cs:C1710.workers.worker
	$CTranslate2:=cs:C1710.workers.worker.new()
	
	If (Not:C34($CTranslate2.isRunning($port)))
		
		If (Value type:C1509($folder)#Is object:K8:27) || (Not:C34(OB Instance of:C1731($folder; 4D:C1709.Folder))) || ($URL="")
			var $homeFolder : 4D:C1709.Folder
			$homeFolder:=Folder:C1567(fk home folder:K87:24).folder(".CTranslate2")
			$folder:=$homeFolder.folder("BAAI/bge-small-en-v1_5_int8_float16")
			$URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/bge-small-en-v1.5_int8_float16.zip"
		End if 
		
		If ($port=0) || ($port<0) || ($port>65535)
			$port:=8080
		End if 
		
		This:C1470.main($port; $folder; $URL; $options; $event)
		
	End if 
	
Function onTCP($status : Object; $options : Object)
	
	If ($status.success)
		
		var $className : Text
		$className:=Split string:C1554(Current method name:C684; "."; sk trim spaces:K86:2).first()
		
		CALL WORKER:C1389($className; Formula:C1597(start); $options; Formula:C1597(onModel))
		
	Else 
		
		var $statuses : Text
		$statuses:="TCP port "+String:C10($status.port)+" is aready used by process "+$status.PID.join(",")
		var $error : cs:C1710.event.error
		$error:=cs:C1710.event.error.new(1; $statuses)
		
		If ($options.event#Null:C1517) && (OB Instance of:C1731($options.event; cs:C1710.event.event))
			$options.event.onError.call(This:C1470; $options; $error)
		End if 
		
	End if 
	
Function main($port : Integer; $folder : 4D:C1709.Folder; $URL : Text; $options : Object; $event : cs:C1710.event.event)
	
	main({port: $port; folder: $folder; URL: $URL; event: $event; options: $options}; This:C1470.onTCP)
	
Function terminate()
	
	var $CTranslate2 : cs:C1710.workers.worker
	$CTranslate2:=cs:C1710.workers.worker.new(cs:C1710._server)
	$CTranslate2.terminate()