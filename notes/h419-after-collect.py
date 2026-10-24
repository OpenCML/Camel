# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

import json, re, collections, sys

rows = json.load(open(sys.argv[1]))
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
print('total', len(rows))
for k, v in buck.most_common(60):
    print(f'{v:5d}  {k}')