// The reference of the visibility fixture: `tool = "node"` is not a Python child, so the runner
// leaves this process's environment exactly as it found it, and the declared Python test of
// visibility.plan.toml must print the same line. Prints `<absent>` for a variable the environment
// does not hold.
const value = process.env.CUDA_VISIBLE_DEVICES
console.log(`CUDA_VISIBLE_DEVICES=[${value === undefined ? '<absent>' : value}]`)