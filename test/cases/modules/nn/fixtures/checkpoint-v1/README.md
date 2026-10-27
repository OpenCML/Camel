# `checkpoint-v1`: the checkpoint format the tree library rewrite must keep reading

`test/cases/modules/nn/checkpoint_fixture_load.cml` loads this directory with `ckpt:load_tree`
and `checkpoint_fixture.mjs` asserts the values and that the bytes are unchanged. It is the
compatibility check the Camel tree library cannot make: the bytes were produced by the native
`ckpt` writer and are committed data, so the loader must keep reading them as the tree code
around it changes. The rewrite changes `stdlib/tree.cml` only, not the format; these bytes are
therefore what a pre-rewrite checkpoint looks like.

Regenerate only when the checkpoint format is deliberately versioned. The writer used was:

```camel
import { Tensor, new, zeros } from tensor
import { save_tree } from ckpt

type Record = {
    w: f32[..], odd: int[..], empty: f32[..],
    note: string, cursor: (int, float), enabled: bool
}

func main(dir: string): int sync {
    let record: Record = {
        w: new([1.0, -2.0]), odd: new([9007199254740993, -2]), empty: zeros([2, 0]),
        note: '中文 checkpoint', cursor: (3, 0.5), enabled: true
    }
    save_tree(dir, record)
    return 0
}
```

```console
camel checkpoint_fixture_write.cml --arg dir=test/cases/modules/nn/fixtures/checkpoint-v1 std::nvm
```

The manifest's file names are the paths `tree_paths` gives: `w`, `odd`, `empty`, `note`,
`cursor.0`, `cursor.1`, `enabled`.