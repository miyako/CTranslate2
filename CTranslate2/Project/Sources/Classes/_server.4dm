Class extends _CTranslate2

Class constructor($controller : 4D:C1709.Class)
	
	Super:C1705($controller)
	
Function start($option : Object) : 4D:C1709.SystemWorker
	
	This:C1470.bind($option; ["onTerminate"])
	
	var $command : Text
	$command:=This:C1470.escape(This:C1470.executablePath)
	
	$command+=" -s "
	
	If (Value type:C1509($option.embeddings_model)=Is object:K8:27) && (OB Instance of:C1731($option.embeddings_model; 4D:C1709.Folder)) && ($option.embeddings_model.exists)
		$command+=" -e "
		$command+=This:C1470.escape(This:C1470.expand($option.embeddings_model).path)
	End if 
	
	If (Value type:C1509($option.rerank_model)=Is object:K8:27) && (OB Instance of:C1731($option.rerank_model; 4D:C1709.Folder)) && ($option.rerank_model.exists)
		$command+=" -r "
		$command+=This:C1470.escape(This:C1470.expand($option.rerank_model).path)
	End if 
	
	If (Value type:C1509($option.chat_model)=Is object:K8:27) && (OB Instance of:C1731($option.chat_model; 4D:C1709.Folder)) && ($option.chat_model.exists)
		$command+=" -g "
		$command+=This:C1470.escape(This:C1470.expand($option.chat_model).path)
	End if 
	
	If (Value type:C1509($option.translate_model)=Is object:K8:27) && (OB Instance of:C1731($option.translate_model; 4D:C1709.Folder)) && ($option.translate_model.exists)
		$command+=" -m "
		$command+=This:C1470.escape(This:C1470.expand($option.translate_model).path)
	End if 
	
	If (Value type:C1509($option.generate_model)=Is object:K8:27) && (OB Instance of:C1731($option.generate_model; 4D:C1709.Folder)) && ($option.generate_model.exists)
		$command+=" -a "
		$command+=This:C1470.escape(This:C1470.expand($option.generate_model).path)
	End if 
	
	If (Value type:C1509($option.port)=Is real:K8:4) && ($option.port#0)
		$command+=" -p "
		$command+=String:C10($option.port)
	End if 
	
	If (Value type:C1509($option.host)=Is text:K8:3) && ($option.host#"")
		$command+=" -h "
		$command+=$option.host
	End if 
	
	var $chat_template : Variant
	Case of 
		: (Value type:C1509($option.chat_template)=Is text:K8:3) && ($option.chat_template#"")
			//-j reads the template from stdin until EOF; execute() posts it and closes the input
			$command+=" -j "
			$chat_template:=$option.chat_template
		: (Value type:C1509($option.chat_template)=Is object:K8:27) && (OB Instance of:C1731($option.chat_template; 4D:C1709.File)) && ($option.chat_template.exists)
			$command+=" -t "
			$command+=This:C1470.escape(This:C1470.expand($option.chat_template).path)
	End case 
	
	If (Value type:C1509($option.pooling)=Is text:K8:3) && ($option.pooling#"")
		Case of 
			: ($option.pooling="cls")
				$command+=" -c "
			: ($option.pooling="mean")
				//default
			: ($option.pooling="last-token")
				$command+=" -l "
			: ($option.pooling="multi-vector")  //ColBERT
				//not implemented
			: ($option.pooling="splade")
				//not implemented
			: ($option.pooling="e2e")  //Universal Sentence Encoder
				//not implemented
		End case 
	End if 
	
	//ct2-server has no long options: getopt reads "--key" as "-" (read stdin) and the server never starts listening
	//other keys in $option are therefore not passed on the command line
	
	//SET TEXT TO PASTEBOARD($command)
	
	return This:C1470.controller.execute($command; $chat_template).worker
	