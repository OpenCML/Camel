# A dump excerpt whose node is not the node the program reaches

This page is a fixture, not a reader-facing page: the excerpt below matches its program's dump in
every number, and calls the printing operator `:io/print` where the program reaches `:io/println`.
The renaming is about numbers only, so this page must fail.

```cml
func main(): int sync {
    println(1)
    return 0
}
```

```bash
camel dump_content_differs.cml std::gir
```

```text
    subgraph cluster_F01 {
        label="main";
        O01 [label="EXIT", type="EXIT"];
        N00 [label="1", type="DATA", origin=35, span="1:12-1:13", srcfile="./dump_content_differs.cml", value_type="int"];
        N01 [label=":io/print", type="OPER", origin=36, span="1:4-1:11", srcfile="./dump_content_differs.cml", value_type="int"];
        N02 [label="0", type="DATA", origin=37, span="2:11-2:12", srcfile="./dump_content_differs.cml", value_type="int"];
        N03 [label="GATE", type="GATE", value_type="int"];
        N03 -> O01 [label="0", type="EXIT"];
        N00 -> N01 [label="0|0", type="NORM"];
        N02 -> N03 [label="0|0", type="NORM"];
        N01 -> N03 [label="0|0", type="CTRL"];
    }
```