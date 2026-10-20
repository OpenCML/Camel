#!/usr/bin/env node
// Precondition of the tests that assert Windows' wide-character process boundary
// (`skip_unless = ["node", "test/tools/on-windows.mjs"]`).
//
// A path holding characters outside the process code page (the point of H9 item 3) is a Windows
// question: elsewhere a path is bytes, and there is no code page to lose them to. Exit 0 on
// Windows, 1 with the reason elsewhere.

if (process.platform === 'win32') {
    process.exit(0)
}
console.log(`this test asserts Windows' wide-character process boundary: not on ${process.platform}`)
process.exit(1)