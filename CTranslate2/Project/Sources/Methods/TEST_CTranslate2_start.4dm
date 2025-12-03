//%attributes = {"invisible":true}
var $CTranslate2 : cs:C1710.CTranslate2

/*
typically On Startup
the ct2 model is downloaded automatically if necessary
ct2-embedding-cli is started in server mode when the model is ready
*/

If (True:C214)
	$CTranslate2:=cs:C1710.CTranslate2.new()  //default
Else 
	var $modelsFolder : 4D:C1709.Folder
	$modelsFolder:=Folder:C1567(fk home folder:K87:24).folder(".CTranslate2")
	$folder:=$modelsFolder.folder("sentence-transformers/paraphrase-multilingual-mpnet-base-v2")
	var $URL : Integer
	$URL:="https://github.com/miyako/ct2-embedding-cli/releases/download/models/medium.zip"
	var $port : Integer
	$port:=8080
	$CTranslate2:=cs:C1710.CTranslate2.new($port; $folder; $URL)
End if 