Class extends _interface

Class constructor($port : Integer; $huggingfaces : cs:C1710.event.huggingfaces; $HOME : 4D:C1709.Folder; $options : Object; $event : cs:C1710.event.event)
	
	Super:C1705()
	
	var $CTranslate2 : cs:C1710.workers.worker
	$CTranslate2:=cs:C1710.workers.worker.new()
	
	If (Not:C34($CTranslate2.isRunning($port)))
		
		If (Not:C34(OB Instance of:C1731($HOME; 4D:C1709.Folder))) || (Not:C34($HOME.exists))
			$HOME:=Folder:C1567(fk home folder:K87:24).folder(".CTranslate2")
		End if 
		
		If ($huggingfaces=Null:C1517) || (Not:C34(OB Instance of:C1731($huggingfaces; cs:C1710.event.huggingfaces))) || ($huggingfaces.huggingfaces.length=0)
			
			$folder:=$HOME.folder("all-MiniLM-L6-v2-ct2-int8_float16")
			$path:="keisuke-miyako/all-MiniLM-L6-v2-ct2-int8_float16"
			$URL:="keisuke-miyako/all-MiniLM-L6-v2-ct2-int8_float16"
			$embeddings:=cs:C1710.event.huggingface.new($folder; $URL; $path; "embedding")
			
			$huggingfaces:=cs:C1710.event.huggingfaces.new([$embeddings])
			$options:={}
		End if 
		
		If ($port=0) || ($port<0) || ($port>65535)
			$port:=8080
		End if 
		
		This:C1470._main($port; $huggingfaces; $HOME; $options; $event)
		
	End if 
	
Function _main($port : Integer; $huggingfaces : cs:C1710.event.huggingfaces; $HOME : 4D:C1709.Folder; $options : Object; $event : cs:C1710.event.event)
	
	main({name: Split string:C1554(Current method name:C684; "."; sk trim spaces:K86:2).first(); port: $port; huggingfaces: $huggingfaces; HOME: $HOME; options: $options; event: $event}; This:C1470._onTCP)