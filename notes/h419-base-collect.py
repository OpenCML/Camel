# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

import os, glob, sys, json, collections, re

root = os.path.expanduser('~/camel-work/h419-flip-next/test/results/log')
best, bestn = None, -1
for D in glob.glob(os.path.join(root, '*/')):
    n = len(glob.glob(os.path.join(D, 'fail', '*', '*')))
    if n > bestn:
        best, bestn = D, n
print('DIR', best, 'fail-leaves', bestn)
rows = []
for leaf in sorted(glob.glob(os.path.join(best, 'fail', '*', '*'))):
    plan = os.path.basename(os.path.dirname(leaf))
    name = os.path.basename(leaf)
    meta = {}
    mp = os.path.join(leaf, 'meta.json')
    if os.path.exists(mp):
        meta = json.load(open(mp))
    out = ''
    for f in ('stdout.txt', 'output.txt'):
        p = os.path.join(leaf, f)
        if os.path.exists(p):
            out = open(p, encoding='utf-8', errors='replace').read()
            break
    rows.append({'plan': plan, 'name': name, 'meta': meta, 'out': out})
json.dump(rows, open('/tmp/h419-base-reds.json', 'w'))
# the runner's own red names, in the ledger's snapshot form
with open('/tmp/h419-base-reds.txt', 'w') as f:
    for r in rows:
        f.write(r['plan'] + '/' + r['name'] + '\n')
print('rows', len(rows), '-> /tmp/h419-base-reds.json, /tmp/h419-base-reds.txt')

ERR = re.compile(r'\[(Error|Fatal)\]:\s*(.*)')
def firsterr(out):
    for ln in out.splitlines():
        m = ERR.search(ln)
        if m:
            return m.group(2).strip()
    return None
buck = collections.Counter()
for r in rows:
    m = r['meta']
    names = [x.get('name') for x in (m.get('diagnostics') or []) if isinstance(x, dict)]
    e = firsterr(r['out'] or '')
    if names:
        k = 'diag:' + names[0]
    elif m.get('signal'):
        k = 'signal:' + str(m['signal'])
    elif e:
        k = 'err:' + e[:70]
    else:
        k = 'NONE'
    buck[k] += 1
print('=== buckets ===')
for k, v in buck.most_common(60):
    print(f'{v:5d}  {k}')