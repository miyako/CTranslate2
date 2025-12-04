property URL : Text
property method : Text
property headers : Object
property dataType : Text
property automaticRedirections : Boolean
property folder : 4D:C1709.Folder
property port : Integer
property _onResponse : 4D:C1709.Function

Class constructor($port : Integer; $folder : 4D:C1709.Folder; $URL : Text; $formula : 4D:C1709.Function)
	
	This:C1470.folder:=$folder
	This:C1470.URL:=$URL
	This:C1470.method:="GET"
	This:C1470.headers:={Accept: "application/vnd.github+json"}
	This:C1470.dataType:="blob"
	This:C1470.automaticRedirections:=True:C214
	This:C1470.port:=$port
	This:C1470._onResponse:=$formula
	
	If (OB Instance of:C1731(This:C1470.folder; 4D:C1709.Folder))
		If (Not:C34(This:C1470.folder.exists))
			If (This:C1470.folder.parent#Null:C1517)
				This:C1470.folder.parent.create()
				4D:C1709.HTTPRequest.new(This:C1470.URL; This:C1470)
			End if 
		Else 
			This:C1470.start()
		End if 
	End if 
	
Function start()
	
	var $CTranslate2 : cs:C1710._worker
	$CTranslate2:=cs:C1710._worker.new()
	
	$CTranslate2.start({\
		model: This:C1470.folder; port: This:C1470.port})
	
	If (OB Instance of:C1731(This:C1470._onResponse; 4D:C1709.Function))
		This:C1470._onResponse.call(This:C1470; {success: True:C214})
	End if 
	
	KILL WORKER:C1390
	
Function onResponse($request : 4D:C1709.HTTPRequest; $event : Object)
	
	If ($request.response.status=200) && ($request.dataType="blob")
		var $file; $item : 4D:C1709.File
		$file:=This:C1470.folder.file("model.zip")
		$file.setContent($request.response.body)
		var $archive : 4D:C1709.ZipArchive
		$archive:=ZIP Read archive:C1637($file)
		For each ($item; $archive.root.files(fk ignore invisible:K87:22))
			$item.copyTo(This:C1470.folder)
		End for each 
		$file.delete()
		This:C1470.start()
	End if 
	
Function onError($request : 4D:C1709.HTTPRequest; $event : Object)
	
	If (OB Instance of:C1731(This:C1470._onResponse; 4D:C1709.Function))
		This:C1470._onResponse.call(This:C1470; {success: False:C215})
	End if 