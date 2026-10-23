// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A case that writes the path it is given, and names a fixed temporary path of its own. The
// fixtures under bad/ name it with a fixed place and with ${test_tmp} respectively.
const log = '/tmp/selftest-load-class.log'
const target = process.argv[2] ?? ''
if (target === '') {
    console.log('no path given')
    process.exit(2)
}
process.exit(0)