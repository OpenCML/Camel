# A dump excerpt whose nodes are numbered differently

This page is a fixture, not a reader-facing page: the excerpt below is its own program's dump with
the node and cluster numbers written differently, which is a match: an excerpt does not compare numbers, and one one-to-one renaming is enough.

```cml
func main(): int sync {
    println(1)
    return 0
}
```

```bash
camel dump_renumbered.cml std::gir
```

```text
    subgraph cluster_F7A {
        label="main";
        O7A [label="EXIT", type="EXIT"];
        N7A [label="1", type="DATA", origin=35, span="1:12-1:13", srcfile="./dump_renumbered.cml", value_type="int"];
        N7B [label=":io/println", type="OPER", origin=36, span="1:4-1:11", srcfile="./dump_renumbered.cml", value_type="int"];
        N7C [label="0", type="DATA", origin=37, span="2:11-2:12", srcfile="./dump_renumbered.cml", value_type="int"];
        N7D [label="GATE", type="GATE", value_type="int"];
        N7D -> O7A [label="0", type="EXIT"];
        N7A -> N7B [label="0|0", type="NORM"];
        N7C -> N7D [label="0|0", type="NORM"];
        N7B -> N7D [label="0|0", type="CTRL"];
    }
```