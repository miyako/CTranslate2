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
	
	var $event : cs:C1710.event.event
	$event:=cs:C1710.event.event.new()
/*
Function onError($params : Object; $error : cs.event.error)
Function onSuccess($params : Object; $models : cs.event.models)
Function onData($request : 4D.HTTPRequest; $event : Object)
Function onResponse($request : 4D.HTTPRequest; $event : Object)
Function onTerminate($worker : 4D.SystemWorker; $params : Object)
Function onStdOut($worker : 4D.SystemWorker; $params : Object)
Function onStdErr($worker : 4D.SystemWorker; $params : Object)
*/
	$event.onError:=Formula:C1597(ALERT:C41($2.message))
	$event.onSuccess:=Formula:C1597(ALERT:C41($2.models.extract("name").join(",")+" loaded!"))
	If (False:C215)
		//MARK: all callbacks are preemptive
		$event.onData:=Formula:C1597(MESSAGE:C88(String:C10((This:C1470.range.end/This:C1470.range.length)*100; "###.00%")))
		$event.onResponse:=Formula:C1597(ERASE WINDOW:C160)
	End if 
	$event.onStdOut:=Formula:C1597($2.data)
	$event.onStdErr:=Formula:C1597($2.data)
	If (False:C215)
		//MARK: ALERT is thread safe but dangerous to use on exit...
		$event.onTerminate:=Formula:C1597(ALERT:C41(["process"; $1.pid; "terminated!"].join(" ")))
	End if 
	
	$options:={}
	
	$CTranslate2:=cs:C1710.CTranslate2.new($port; $folder; $URL; $options; $event)
End if 