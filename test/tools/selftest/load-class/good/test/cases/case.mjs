// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A case that writes the path it is given. The fixtures under bad/ and good/ name it with a
// fixed place and with ${test_tmp} respectively.
const target = process.argv[2] ?? ''
if (target === '') {
    console.log('no path given')
    process.exit(2)
}
process.exit(0)