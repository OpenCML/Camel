// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Editor modules use filename identity, including native declarations without module headers.
import fs from 'node:fs'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
import { withServer, checkEq, REPO_ROOT } from '../../../tools/lsp/client.mjs'

await withServer(async (ls) => {
    // Naming the entry "main" breaks both the self-qualified reference and native lookup.
    const uri = ls.open('identity.cml', 'func value(): int { return 7 }\nfunc main(): int { return identity::value() }\n')
    const errors = async (uri) => (await ls.request('textDocument/diagnostic', {
        textDocument: { uri },
    })).items.filter(d => d.severity === 1)
    checkEq(await errors(uri), [], 'the entry resolves its own filename-qualified names')
    const home = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
    const file = path.join(home, 'stdlib', 'bytes.cml')
    const nativeUri = pathToFileURL(file).href
    ls.notify('textDocument/didOpen', { textDocument: {
        uri: nativeUri, languageId: 'camel', version: 1, text: fs.readFileSync(file, 'utf8'),
    } })
    checkEq(await errors(nativeUri), [], 'native declarations resolve operators in their file module')
})
