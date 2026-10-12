// Stands in for `camel` in the runner's self-test: prints the same thing for every back end
// except the one the mode singles out.
const [mode, backend] = process.argv.slice(2)
const diagnostic = (column) =>
    `m.cml:3:${column}: [Error]: Attempted division by zero. (name=RuntimeDiag::DivisionByZero, code=0x04000001)`

if (mode === 'same') {
    console.log('42')
} else if (mode === 'stdout_differs_on_jit') {
    console.log(backend === 'std::jit' ? '41' : '42')
} else if (mode === 'column_differs_on_fvm') {
    console.error(diagnostic(backend === 'std::fvm' ? 9 : 7))
    process.exit(1)
} else if (mode === 'exit_differs_on_prl') {
    console.log('42')
    process.exit(backend === 'std::prl' ? 3 : 0)
} else {
    console.error(`unknown mode ${mode}`)
    process.exit(2)
}
