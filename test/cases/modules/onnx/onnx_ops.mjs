// Minimal ONNX ModelProto reader for test verification: lists node op types,
// initializer names, and graph input/output names without a protobuf library.
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

// ModelProto.graph = 7; GraphProto: node = 1, initializer = 5, input = 11, output = 12;
// NodeProto.op_type = 4; TensorProto.name = 8; ValueInfoProto.name = 1.
export function readOnnxModel(path) {
    const buf = fs.readFileSync(path)
    const model = { ops: [], initializers: [], inputs: [], outputs: [] }
    for (const f of fields(buf)) {
        if (f.field !== 7 || f.wire !== 2) continue
        for (const g of fields(buf, f.start, f.end)) {
            if (g.wire !== 2) continue
            if (g.field === 1) model.ops.push(firstString(buf, g, 4))
            else if (g.field === 5) model.initializers.push(firstString(buf, g, 8))
            else if (g.field === 11) model.inputs.push(firstString(buf, g, 1))
            else if (g.field === 12) model.outputs.push(firstString(buf, g, 1))
        }
    }
    return model
}
