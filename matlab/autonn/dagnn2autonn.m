function netOutputs = dagnn2autonn(dag)
%DAGNN2AUTONN
%   Converts a DagNN object into recursively nested Layer objects.
%   Returns a cell array of Layer objects, each corresponding to an output
%   of the network. These can be composed with other layers, or compiled
%   into a Net object for training/evaluation.
%
%   Example:
%     layers = dagnn2autonn(myDag) ;
%     net = Net(layers{:}) ;
%     net.setInputs({'images', randn(5,5,1,3), 'labels', 1:3}) ;
%     net.eval() ;

  dag.rebuild() ;
  
  % like initParams, but does not overwrite them if already defined
  initUninitializedParams(dag) ;
  
  assert(all(cellfun(@isscalar, {dag.layers.outputIndexes})), ...
    'Layers with 0 or multiple outputs are not supported yet.') ;

  % one-to-out relationship between each layer and its output var
  idx = [dag.layers.outputIndexes] ;
  var2layer = zeros(size(dag.vars)) ;
  var2layer(idx) = 1:numel(idx) ;
  
  % Layer handle corresponding to each DAG layer, empty if not processed
  layers = cell(1, numel(dag.layers)) ;
  
  % create all Param objects
  allParams = cell(size(dag.params)) ;
  for k = 1:numel(dag.params)
    p = dag.params(k) ;
    allParams{k} = Param('name', p.name, 'value', p.value, ...
      'learningRate', p.learningRate, 'weightDecay', p.weightDecay, ...
      'trainMethod', p.trainMethod) ;
  end
  
  % create Input objects for network inputs, corresponding to sources in
  % the graph
  netInputsIdx = find([dag.vars.fanin] == 0) ;
  netInputs = cell(1, numel(netInputsIdx));
  for k = 1:numel(netInputsIdx)
    netInputs{k} = Input(dag.vars(netInputsIdx(k)).name) ;
    var2layer(netInputsIdx(k)) = numel(layers) + k ;  % Inputs are at the end of the layers list
  end
  layers = [layers, netInputs] ;
  
  % find sinks in the graph, these are the network outputs
  netOutputsIdx = find([dag.vars.fanout] == 0) ;

  % recursively process the layers, starting with the network outputs
  netOutputs = cell(1, numel(netOutputsIdx)) ;
  for o = 1:numel(netOutputsIdx)
    netOutputs{o} = convertLayer(dag, var2layer(netOutputsIdx(o))) ;
  end
  
  % copy meta properties to one of the Layers
  netOutputs{1}.meta = dag.meta ;
  
  
  % process a single layer, and recurse on its inputs
  function obj = convertLayer(dag, layerIdx)
    % if this layer has already been processed, return its handle
    if ~isempty(layers{layerIdx})
      obj = layers{layerIdx} ;
      return
    end
    
    layer = dag.layers(layerIdx) ;
    params = allParams(layer.paramIndexes) ;
    block = layer.block ;
    
    % recurse on inputs; they must be defined before this Layer
    inputVars = layer.inputIndexes ;
    inputs = cell(size(inputVars)) ;
    for i = 1:numel(inputVars)
      inputs{i} = convertLayer(dag, var2layer(inputVars(i))) ;
    end
    
    % now create a Layer with those inputs and parameters
    
    if isa(block, 'dagnn.Conv')
      if isscalar(params)  % no bias
        params{2} = [] ;
      end
      obj = vl_nnconv(inputs{1}, params{1}, params{2}, ...
        'pad', block.pad, 'stride', block.stride, block.opts{:}) ;
    
    elseif isa(block, 'dagnn.Pooling')
      obj = vl_nnpool(inputs{1}, block.poolSize, 'method', block.method, ...
        'pad', block.pad, 'stride', block.stride, block.opts{:}) ;
    
    elseif isa(block, 'dagnn.BatchNorm')
      obj = vl_nnbnorm(inputs{1}, params{1}, params{2}, params{3}, ...
        'epsilon', block.epsilon) ;
    
    elseif isa(block, 'dagnn.ReLU')
      obj = vl_nnrelu(inputs{1}, 'leak', block.leak, block.opts{:}) ;
      
    elseif isa(block, 'dagnn.LRN')
      obj = vl_nnnormalize(inputs{1}, block.param) ;
      
    elseif isa(block, 'dagnn.DropOut')
      obj = vl_nndropout(inputs{1}) ;
      
    elseif isa(block, 'dagnn.Loss')
      obj = vl_nnloss(inputs{1}, inputs{2}, 'loss', block.loss, block.opts{:}) ;
      
    else
      error(['Unknown block type ''' class(block) '''.']) ;
    end
    
    obj.name = layer.name ;
    layers{layerIdx} = obj ;
  end
end

function initUninitializedParams(obj)
  % Same as INITPARAMS, but does not replace parameters that were already
  % initialized

  for l = 1:numel(obj.layers)
    p = obj.getParamIndex(obj.layers(l).params) ;
    params = obj.layers(l).block.initParams() ;
    switch obj.device
      case 'cpu'
        params = cellfun(@gather, params, 'UniformOutput', false) ;
      case 'gpu'
        params = cellfun(@gpuArray, params, 'UniformOutput', false) ;
    end
    
    for i = 1:numel(params)
      if isempty(obj.params(p(i)).value)
        obj.params(p(i)).value = params{i} ;
      end
    end
  end
end