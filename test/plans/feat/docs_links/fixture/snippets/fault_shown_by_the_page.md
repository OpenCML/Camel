# A page that shows its program faulting

This page is a fixture, not a reader-facing page: the program reads past the end of its array, and
the `text` block quotes what the run prints before it and what the compiler reports about it. The
compile step stops before the program runs; the run step still compares the block, both the printed
lines and the located diagnostic.

```cml
// fault.cml
func main() => int sync {
    let xs = [1, 2, 3]
    println('before')
    println(xs[5])
    return 0
}
```

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
  --> fault.cml:5:15
   |
 5 |     println(xs[5])
   |               ^
help: Ensure the index is within the valid range of the collection.
```