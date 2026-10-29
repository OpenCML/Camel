// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What one test child sees of the cards: the variable the runner writes when it chose a card for
// the test, and the order it writes beside it. One line each, so a plan can pin the whole answer
// instead of searching it. `<unset>` is printed rather than the empty string, so a variable the
// runner did not write and one it wrote empty are told apart.
const visible = process.env.CUDA_VISIBLE_DEVICES
const order = process.env.CUDA_DEVICE_ORDER
console.log(`CUDA_VISIBLE_DEVICES=${visible === undefined ? '<unset>' : visible}`)
console.log(`CUDA_DEVICE_ORDER=${order === undefined ? '<unset>' : order}`)