# A graph dump that is not the graph of its program

This page is a fixture, not a reader-facing page: the dump below is deliberately not what the
program prints, so that the check that compares them has something to fail on.

```cml
func main(): int sync {
    println(1)
    return 0
}
```

```bash
camel dump_stale.cml std::gir
```

```text
digraph GraphIR {
    N99 [label="a node this program does not have", type="DATA", value_type="int"];
}
```