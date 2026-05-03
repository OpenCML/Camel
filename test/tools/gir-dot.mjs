function parseAttributes(raw) {
    const attrs = {}
    for (const part of raw.split(',')) {
        const segment = part.trim()
        if (!segment) continue
        const idx = segment.indexOf('=')
        if (idx < 0) continue
        const key = segment.slice(0, idx).trim()
        let value = segment.slice(idx + 1).trim()
        value = value.replace(/^"/, '').replace(/"$/, '')
        attrs[key] = value
    }
    return attrs
}

function parseDot(dot) {
    const nodes = new Map()
    const edges = []
    const lines = dot.split(/\r?\n/)
    for (const raw of lines) {
        const line = raw.trim()
        if (!line || line === '{' || line === '}' || line.startsWith('digraph ')) continue
        const edge = line.match(/^("?[^"\s;]+"?)\s*->\s*("?[^"\s;]+"?)\s*(?:\[(.+)\])?;?$/)
        if (edge) {
            const [, fromRaw, toRaw, attrsRaw = ''] = edge
            const from = fromRaw.replace(/^"/, '').replace(/"$/, '')
            const to = toRaw.replace(/^"/, '').replace(/"$/, '')
            edges.push({ from, to, attrs: parseAttributes(attrsRaw) })
            if (!nodes.has(from)) nodes.set(from, {})
            if (!nodes.has(to)) nodes.set(to, {})
            continue
        }
        const node = line.match(/^("?[^"\s;]+"?)\s*\[(.+)\];?$/)
        if (node) {
            const [, idRaw, attrsRaw] = node
            const id = idRaw.replace(/^"/, '').replace(/"$/, '')
            nodes.set(id, parseAttributes(attrsRaw))
        }
    }
    return { nodes, edges }
}

function nodeSignature(attrs, graph, id) {
    const label = attrs.label || ''
    const shape = attrs.shape || ''
    const incoming = graph.edges.filter((edge) => edge.to === id).length
    const outgoing = graph.edges.filter((edge) => edge.from === id).length
    return `${label}|${shape}|in:${incoming}|out:${outgoing}`
}

export function compareGirDotIsomorphic(actualText, expectedText) {
    const actual = parseDot(actualText)
    const expected = parseDot(expectedText)
    if (actual.nodes.size !== expected.nodes.size) {
        return { ok: false, message: `node count mismatch: actual=${actual.nodes.size}, expected=${expected.nodes.size}` }
    }
    if (actual.edges.length !== expected.edges.length) {
        return { ok: false, message: `edge count mismatch: actual=${actual.edges.length}, expected=${expected.edges.length}` }
    }

    const actualNodeSigs = [...actual.nodes.entries()].map(([id, attrs]) => nodeSignature(attrs, actual, id)).sort()
    const expectedNodeSigs = [...expected.nodes.entries()].map(([id, attrs]) => nodeSignature(attrs, expected, id)).sort()
    if (JSON.stringify(actualNodeSigs) !== JSON.stringify(expectedNodeSigs)) {
        return { ok: false, message: 'node signatures differ' }
    }

    const actualEdgeSigs = actual.edges
        .map((edge) => {
            const fromSig = nodeSignature(actual.nodes.get(edge.from) || {}, actual, edge.from)
            const toSig = nodeSignature(actual.nodes.get(edge.to) || {}, actual, edge.to)
            const label = edge.attrs.label || ''
            return `${fromSig}=>${toSig}|${label}`
        })
        .sort()
    const expectedEdgeSigs = expected.edges
        .map((edge) => {
            const fromSig = nodeSignature(expected.nodes.get(edge.from) || {}, expected, edge.from)
            const toSig = nodeSignature(expected.nodes.get(edge.to) || {}, expected, edge.to)
            const label = edge.attrs.label || ''
            return `${fromSig}=>${toSig}|${label}`
        })
        .sort()
    if (JSON.stringify(actualEdgeSigs) !== JSON.stringify(expectedEdgeSigs)) {
        return { ok: false, message: 'edge signatures differ' }
    }
    return { ok: true }
}
