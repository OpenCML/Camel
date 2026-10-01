// Minimal ONNX ModelProto reader for test verification: lists node op types,
// initializer names, graph input/output names, and graph input dims (numbers
// for fixed extents, strings for symbolic ones) without a protobuf library.
import fs from 'fs'

function* fields(buf, start = 0, end = buf.length) {
    let pos = start
    const varint = () => {
        let result = 0n
        let shift = 0n
        for (;;) {
            const byte = buf[pos++]
            result |= BigInt(byte & 0x7f) << shift
            if ((byte & 0x80) === 0) return result
            shift += 7n
        }
    }
    while (pos < end) {
        const key = Number(varint())
        const field = key >> 3
        const wire = key & 7
        if (wire === 0) {
            yield { field, wire, value: varint() }
        } else if (wire === 2) {
            const len = Number(varint())
            yield { field, wire, start: pos, end: pos + len }
            pos += len
        } else if (wire === 1) {
            pos += 8
        } else if (wire === 5) {
            pos += 4
        } else {
            throw new Error(`unsupported wire type ${wire}`)
        }
    }
}

const text = (buf, f) => buf.toString('utf8', f.start, f.end)
const firstString = (buf, f, field) => {
    for (const g of fields(buf, f.start, f.end)) {
        if (g.field === field && g.wire === 2) return text(buf, g)
    }
    return ''
}

/// Dims of a ValueInfoProto: type = 2 -> tensor_type = 1 -> shape = 2 -> dim = 1 (dim_value = 1,
/// dim_param = 2).
const dimsOf = (buf, info) => {
    const dims = []
    const child = (f, field) => {
        for (const g of fields(buf, f.start, f.end)) {
            if (g.field === field && g.wire === 2) return g
        }
        return null
    }
    const type = child(info, 2)
    const tensor = type && child(type, 1)
    const shape = tensor && child(tensor, 2)
    if (!shape) return null
    for (const d of fields(buf, shape.start, shape.end)) {
        if (d.field !== 1 || d.wire !== 2) continue
        let dim = null
        for (const e of fields(buf, d.start, d.end)) {
            if (e.field === 1 && e.wire === 0) dim = Number(e.value)
            if (e.field === 2 && e.wire === 2) dim = text(buf, e)
        }
        dims.push(dim)
    }
    return dims
}

// ModelProto.graph = 7; GraphProto: node = 1, initializer = 5, input = 11, output = 12;
// NodeProto.op_type = 4; TensorProto.name = 8; ValueInfoProto.name = 1.
export function readOnnxModel(path) {
    const buf = fs.readFileSync(path)
    const model = { ops: [], initializers: [], inputs: [], inputDims: [], outputs: [] }
    for (const f of fields(buf)) {
        if (f.field !== 7 || f.wire !== 2) continue
        for (const g of fields(buf, f.start, f.end)) {
            if (g.wire !== 2) continue
            if (g.field === 1) model.ops.push(firstString(buf, g, 4))
            else if (g.field === 5) model.initializers.push(firstString(buf, g, 8))
            else if (g.field === 11) {
                model.inputs.push(firstString(buf, g, 1))
                model.inputDims.push(dimsOf(buf, g))
            }
            else if (g.field === 12) model.outputs.push(firstString(buf, g, 1))
        }
    }
    return model
}
