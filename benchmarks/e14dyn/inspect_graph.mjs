import { readOnnxModel } from '../../test/cases/modules/onnx/onnx_ops.mjs'
const m = readOnnxModel(process.argv[2])
console.log('inputs', m.inputs.join())
console.log('outputs', m.outputs.join())
console.log('inputDims', JSON.stringify(m.inputDims))
console.log('initializers', m.initializers.length, m.initializers.slice(0, 12).join())
const count = (op) => m.ops.filter((o) => o === op).length
for (const op of ['MatMul', 'Sub', 'Shape', 'Gather', 'Div', 'Expand', 'ReduceSum', 'Cast']) {
    console.log(op, count(op))
}
console.log('all ops', m.ops.join())
