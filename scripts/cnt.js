#!/usr/bin/env node
/**
 * Code count reporter - runs tokei and formats output with:
 * 1. Markdown excluded from code total
 * 2. Per-folder stats (docs, include, src, test, tools, scripts, etc.)
 * 3. Compact table for narrow terminals
 */
import { spawnSync } from 'child_process'
import path from 'path'
import { fileURLToPath } from 'url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const rootDir = path.dirname(__dirname)

const EXCLUDES = ['third_party', 'package-lock.json']
const EXCLUDE_CODE_LANGS = ['Markdown', 'Autoconf', 'SVG', 'Plain Text']
const COL_WIDTHS = { lang: 14, files: 7, lines: 7, code: 7, cmnt: 7, blank: 7 }
const MAX_COL = 55

function runTokeiJson(inputDirs = []) {
    const args = []
    for (const e of EXCLUDES) args.push('--exclude', e)
    args.push('-o', 'json')
    if (inputDirs.length > 0) args.push(...inputDirs)
    const r = spawnSync('tokei', args, { cwd: rootDir, encoding: 'utf-8', shell: true })
    if (r.error) throw r.error
    if (r.status !== 0) throw new Error(`tokei exited ${r.status}: ${r.stderr}`)
    const out = r.stdout
    const line = out.split('\n').find((l) => l.trim().startsWith('{'))
    if (!line) throw new Error('tokei did not output valid JSON')
    return JSON.parse(line)
}

function getTopFolder(filePath) {
    const normalized = filePath.replace(/^\.[\\/]/, '').replace(/\\/g, '/')
    const idx = normalized.indexOf('/')
    return idx < 0 ? 'root' : normalized.slice(0, idx)
}

function aggregateByFolder(data) {
    const byFolder = new Map()
    for (const [lang, entry] of Object.entries(data)) {
        if (lang === 'Total' || !entry.reports) continue
        for (const r of entry.reports) {
            const folder = getTopFolder(r.name)
            if (!byFolder.has(folder)) {
                byFolder.set(folder, { files: 0, code: 0, comments: 0, blanks: 0, langs: {} })
            }
            const stats = r.stats || {}
            const rec = byFolder.get(folder)
            rec.files += 1
            rec.code += stats.code ?? 0
            rec.comments += stats.comments ?? 0
            rec.blanks += stats.blanks ?? 0
            if (!rec.langs[lang]) rec.langs[lang] = { files: 0, code: 0, comments: 0, blanks: 0 }
            rec.langs[lang].files += 1
            rec.langs[lang].code += stats.code ?? 0
            rec.langs[lang].comments += stats.comments ?? 0
            rec.langs[lang].blanks += stats.blanks ?? 0
        }
    }
    return byFolder
}

function fmtNum(n) {
    return String(n).padStart(COL_WIDTHS.code - 1)
}

function printTable(rows, title) {
    const { lang, files, lines, code, cmnt, blank } = COL_WIDTHS
    const sep = '-'.repeat(MAX_COL)
    console.log(`\n${title}`)
    console.log(sep)
    console.log(
        ' Lang'.padEnd(lang + 1) +
            'Files'.padStart(files) +
            'Lines'.padStart(lines + 1) +
            'Codes'.padStart(code + 1) +
            'Cmnts'.padStart(cmnt + 1) +
            'Blank'.padStart(blank + 1)
    )
    console.log(sep)
    for (const r of rows) {
        const linesVal = (r.code ?? 0) + (r.comments ?? 0) + (r.blanks ?? 0)
        console.log(
            (' ' + r.lang).padEnd(lang + 1) +
                String(r.files).padStart(files) +
                fmtNum(linesVal).padStart(lines + 1) +
                fmtNum(r.code ?? 0).padStart(code + 1) +
                fmtNum(r.comments ?? 0).padStart(cmnt + 1) +
                fmtNum(r.blanks ?? 0).padStart(blank + 1)
        )
    }
    console.log(sep)
}

function main() {
    const data = runTokeiJson()
    const total = data.Total || {}

    const codeRows = []
    let totalCode = 0
    let totalFiles = 0
    let totalCmnt = 0
    let totalBlank = 0
    const langOrder = ['C++', 'C Header', 'JavaScript', 'CMake', 'Python', 'JSON', 'TOML']

    for (const lang of langOrder) {
        const entry = data[lang]
        if (!entry || EXCLUDE_CODE_LANGS.includes(lang)) continue
        const code = entry.code ?? 0
        const comments = entry.comments ?? 0
        const blanks = entry.blanks ?? 0
        const files = entry.reports?.length ?? 0
        if (files === 0 && code === 0) continue
        codeRows.push({
            lang,
            files,
            code,
            comments,
            blanks
        })
        totalCode += code
        totalFiles += files
        totalCmnt += comments
        totalBlank += blanks
    }

    for (const [lang, entry] of Object.entries(data)) {
        if (lang === 'Total' || langOrder.includes(lang) || EXCLUDE_CODE_LANGS.includes(lang)) continue
        const code = entry.code ?? 0
        const comments = entry.comments ?? 0
        const blanks = entry.blanks ?? 0
        const files = entry.reports?.length ?? 0
        if (files === 0 && code === 0) continue
        codeRows.push({ lang, files, code, comments, blanks })
        totalCode += code
        totalFiles += files
        totalCmnt += comments
        totalBlank += blanks
    }

    const linesOf = (r) => (r.code ?? 0) + (r.comments ?? 0) + (r.blanks ?? 0)
    codeRows.sort((a, b) => linesOf(b) - linesOf(a))

    const byFolder = aggregateByFolder(data)
    const folderOrder = ['src', 'include', 'tools', 'scripts', 'test', 'docs', 'cmake', 'antlr', 'pypi', 'assets', 'root']
    const allFolders = [...new Set([...folderOrder, ...byFolder.keys()])].filter((f) => byFolder.has(f))

    const foldersWithLines = allFolders
        .map((folder) => {
            const rec = byFolder.get(folder)
            if (!rec || (rec.code === 0 && rec.comments === 0 && rec.blanks === 0)) return null
            const showAllLangs = folder === 'docs'
            const subRows = Object.entries(rec.langs)
                .filter(([l]) => showAllLangs || !EXCLUDE_CODE_LANGS.includes(l))
                .filter(([, s]) => s.code > 0 || s.comments > 0 || s.blanks > 0)
                .map(([, s]) => s)
            if (subRows.length === 0) return null
            const total = subRows.reduce((s, s2) => s + s2.code + s2.comments + s2.blanks, 0)
            return { folder, rec, total }
        })
        .filter(Boolean)
        .sort((a, b) => a.total - b.total)

    for (const { folder, rec } of foldersWithLines) {
        const showAllLangs = folder === 'docs'
        const subRows = Object.entries(rec.langs)
            .filter(([l]) => showAllLangs || !EXCLUDE_CODE_LANGS.includes(l))
            .filter(([, s]) => s.code > 0 || s.comments > 0 || s.blanks > 0)
            .map(([lang, s]) => ({
                lang: '  ' + lang,
                files: s.files,
                code: s.code,
                comments: s.comments,
                blanks: s.blanks
            }))
            .sort((a, b) => linesOf(b) - linesOf(a))
        if (subRows.length === 0) continue
        printTable(subRows, ` ${folder}/`)
        const sumFiles = subRows.reduce((s, r) => s + r.files, 0)
        const sumCode = subRows.reduce((s, r) => s + (r.code ?? 0), 0)
        const sumCmnt = subRows.reduce((s, r) => s + (r.comments ?? 0), 0)
        const sumBlank = subRows.reduce((s, r) => s + (r.blanks ?? 0), 0)
        const sumLines = sumCode + sumCmnt + sumBlank
        console.log(
            (' Total').padEnd(COL_WIDTHS.lang + 1) +
                String(sumFiles).padStart(COL_WIDTHS.files) +
                fmtNum(sumLines).padStart(COL_WIDTHS.lines + 1) +
                fmtNum(sumCode).padStart(COL_WIDTHS.code + 1) +
                fmtNum(sumCmnt).padStart(COL_WIDTHS.cmnt + 1) +
                fmtNum(sumBlank).padStart(COL_WIDTHS.blank + 1)
        )
        console.log('-'.repeat(MAX_COL))
    }

    printTable(codeRows, ' Code')
    const totalLines = totalCode + totalCmnt + totalBlank
    console.log(
        (' Total').padEnd(COL_WIDTHS.lang + 1) +
            String(totalFiles).padStart(COL_WIDTHS.files) +
            fmtNum(totalLines).padStart(COL_WIDTHS.lines + 1) +
            fmtNum(totalCode).padStart(COL_WIDTHS.code + 1) +
            fmtNum(totalCmnt).padStart(COL_WIDTHS.cmnt + 1) +
            fmtNum(totalBlank).padStart(COL_WIDTHS.blank + 1)
    )
    console.log('-'.repeat(MAX_COL))
}

main()
