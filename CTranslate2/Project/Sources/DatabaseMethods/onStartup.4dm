var $CTranslate2 : cs:C1710.CTranslate2

If (False:C215)
	$CTranslate2:=cs:C1710.CTranslate2.new()  //default
Else 
	var $homeFolder : 4D:C1709.Folder
	$homeFolder:=Folder:C1567(fk home folder:K87:24).folder(".CTranslate2")
	$folder:=$homeFolder.folder("snowflake/arctic-embed-s_int8_float16")
	$URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/snowflake-arctic-embed-s_int8_float16.zip"
	
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