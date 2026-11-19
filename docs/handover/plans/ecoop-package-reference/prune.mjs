// Drop the test entries that exercise what the package leaves out.
// Input: a file of lines "<plan id> <case name>", the cases that fail in the package and pass on the full build.
// A case name may carry a back-end suffix the runner adds (.nvm, .fvm, .jit, .prl) or a poison prefix;
// an entry is dropped when its own name is the case name or the case name without that suffix.
import fs from 'node:fs'
import path from 'node:path'

const [listFile, root] = process.argv.slice(2)
const pairs = fs.readFileSync(listFile, 'utf8').split('\n').filter(Boolean).map(l => l.split(' '))
const byPlan = new Map()
for (const [plan, name] of pairs) {
    if (!byPlan.has(plan)) byPlan.set(plan, new Set())
    const s = byPlan.get(plan)
    s.add(name)
    s.add(name.replace(/\.(nvm|fvm|jit|prl)$/, ''))
}

function walk(dir, out = []) {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name)
        if (e.isDirectory()) walk(p, out)
        else if (e.name.endsWith('.plan.toml')) out.push(p)
    }
    return out
}

let droppedEntries = 0, droppedPlans = 0
for (const file of walk(path.join(root, 'test/plans'))) {
    const text = fs.readFileSync(file, 'utf8')
    const id = (text.match(/^id\s*=\s*"([^"]+)"/m) || [])[1]
    const pathId = path.relative(path.join(root, 'test/plans'), file).replace(/\.plan\.toml$/, '').split(path.sep).join('.')
    const names = byPlan.get(pathId) || (id && (byPlan.get('feat.' + id) || byPlan.get(id)))
    if (!names) continue
    const parts = text.split(/^(?=\[\[tests\]\])/m)
    const head = parts[0].startsWith('[[tests]]') ? '' : parts.shift()
    const kept = parts.filter(block => {
        const n = (block.match(/^name\s*=\s*"([^"]+)"/m) || [])[1]
        const drop = n && names.has(n)
        if (drop) droppedEntries++
        return !drop
    })
    if (kept.length === 0) {
        fs.rmSync(file)
        droppedPlans++
    } else {
        fs.writeFileSync(file, head + kept.join(''))
    }
}
console.log(`dropped ${droppedEntries} entries, ${droppedPlans} plans emptied and removed`)
