import fs from 'fs'

function stripComment(line) {
    let inString = false
    let quote = ''
    for (let i = 0; i < line.length; i++) {
        const ch = line[i]
        if (inString) {
            if (ch === '\\') {
                i++
                continue
            }
            if (ch === quote) {
                inString = false
                quote = ''
            }
            continue
        }
        if (ch === '"' || ch === "'") {
            inString = true
            quote = ch
            continue
        }
        if (ch === '#') {
            return line.slice(0, i)
        }
    }
    return line
}

function splitTopLevel(value, delimiter = ',') {
    const parts = []
    let current = ''
    let depth = 0
    let inString = false
    let quote = ''
    for (let i = 0; i < value.length; i++) {
        const ch = value[i]
        if (inString) {
            current += ch
            if (ch === '\\') {
                i++
                if (i < value.length) current += value[i]
                continue
            }
            if (ch === quote) {
                inString = false
                quote = ''
            }
            continue
        }
        if (ch === '"' || ch === "'") {
            inString = true
            quote = ch
            current += ch
            continue
        }
        if (ch === '[') depth++
        if (ch === ']') depth--
        if (ch === delimiter && depth === 0) {
            if (current.trim().length > 0) parts.push(current.trim())
            current = ''
            continue
        }
        current += ch
    }
    if (current.trim().length > 0) parts.push(current.trim())
    return parts
}

function parseString(raw) {
    if (raw.length < 2) {
        throw new Error(`invalid string literal: ${raw}`)
    }
    const quote = raw[0]
    const body = raw.slice(1, -1)
    if (quote === "'") {
        return body
    }
    return body
        .replace(/\\n/g, '\n')
        .replace(/\\r/g, '\r')
        .replace(/\\t/g, '\t')
        .replace(/\\"/g, '"')
        .replace(/\\\\/g, '\\')
}

function parseValue(raw) {
    const value = raw.trim()
    if (value.startsWith('"') || value.startsWith("'")) {
        return parseString(value)
    }
    if (value.startsWith('[') && value.endsWith(']')) {
        const inner = value.slice(1, -1).trim()
        if (!inner) return []
        return splitTopLevel(inner).map(parseValue)
    }
    if (value === 'true') return true
    if (value === 'false') return false
    if (/^[+-]?\d+$/.test(value)) return Number.parseInt(value, 10)
    if (/^[+-]?\d+\.\d+$/.test(value)) return Number.parseFloat(value)
    throw new Error(`unsupported TOML value: ${value}`)
}

export function parseTomlString(content) {
    const doc = {}
    let current = doc
    const lines = content.split(/\r?\n/)
    for (const rawLine of lines) {
        const line = stripComment(rawLine).trim()
        if (!line) continue
        const arrayTable = line.match(/^\[\[(.+)\]\]$/)
        if (arrayTable) {
            const key = arrayTable[1].trim()
            if (!Array.isArray(doc[key])) {
                doc[key] = []
            }
            const entry = {}
            doc[key].push(entry)
            current = entry
            continue
        }
        const match = line.match(/^([A-Za-z0-9_.-]+)\s*=\s*(.+)$/)
        if (!match) {
            throw new Error(`unsupported TOML line: ${rawLine}`)
        }
        const [, key, value] = match
        current[key] = parseValue(value)
    }
    return doc
}

export function parseTomlFile(filePath) {
    return parseTomlString(fs.readFileSync(filePath, 'utf8'))
}
