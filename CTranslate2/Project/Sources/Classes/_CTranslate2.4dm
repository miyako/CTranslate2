property port : Integer
property onData : 4D:C1709.Function
property onDataError : 4D:C1709.Function
property onTerminate : 4D:C1709.Function

Class extends _CLI

Class constructor($controller : 4D:C1709.Class)
	
	If (Not:C34(OB Instance of:C1731($controller; cs:C1710._CTranslate2_Controller)))
		$controller:=cs:C1710._CTranslate2_Controller
	End if 
	
	Super:C1705("ct2-embedding-cli"; $controller)
	
Function bind($option : Object; $properties : Collection) : cs:C1710._CLI
	
	var $property : Text
	For each ($property; $properties)
		This:C1470[$property]:=$option[$property]
	End for each 
	
	return This:C1470
	
Function get worker() : 4D:C1709.SystemWorker
	
	return This:C1470.controller.worker
	
Function terminate()
	
	This:C1470.controller.terminate()