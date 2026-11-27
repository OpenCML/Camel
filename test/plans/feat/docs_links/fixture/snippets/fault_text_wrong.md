# A page whose fault text is not what the run prints

This page is a fixture, not a reader-facing page: the program is the one a fault page carries, but
the `text` block declares the wrong index and the wrong column. The run step compares the block it
declares with what the run printed -- printed lines and located diagnostic -- so this page is red
however green the program itself is. It is the negative reading of the comparison the `wholeRun`
change widened: widening what is compared must not let a page whose declared fault text differs pass.

```cml
// fault_wrong.cml
func main() => int sync {
    let xs = [1, 2, 3]
    println('before')
    println(xs[5])
    return 0
}
```

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 4 is out of range for an array of length 3.
  --> fault_wrong.cml:4:11
   |
 4 |     println(xs[4])
   |           ^
help: Ensure the index is within the valid range of the collection.
```