# A dump excerpt that names two nodes the same way

This page is a fixture, not a reader-facing page: the excerpt below writes the number `N7A` on two
different nodes of one dump (the constant `1` and the printing operator). No one-to-one renaming of
its numbers onto the dump's can make it a run of the dump, so this page must fail.

```cml
func main(): int sync {
    println(1)
    return 0
}
```

```bash
camel dump_number_reused.cml std::gir
```

```text
    subgraph cluster_F7A {
        label="main";
        O7A [label="EXIT", type="EXIT"];
        N7A [label="1", type="DATA", origin=35, span="1:12-1:13", srcfile="./dump_number_reused.cml", value_type="int"];
        N7A [label=":io/println", type="OPER", origin=36, span="1:4-1:11", srcfile="./dump_number_reused.cml", value_type="int"];
        N7C [label="0", type="DATA", origin=37, span="2:11-2:12", srcfile="./dump_number_reused.cml", value_type="int"];
        N7D [label="GATE", type="GATE", value_type="int"];
        N7D -> O7A [label="0", type="EXIT"];
        N7A -> N7B [label="0|0", type="NORM"];
        N7C -> N7D [label="0|0", type="NORM"];
        N7B -> N7D [label="0|0", type="CTRL"];
    }
```