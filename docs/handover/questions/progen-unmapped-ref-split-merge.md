# progen seed 24 的 `unmapped live node ref`：两条线的 merge 上才出现

基线 `origin/develop` `6218b4ee4`。本页的读数全部在远端主机上用 `camel-remote.ps1 -Jobs 8` 构建并跑 `test/plans/feat/opt/progen.plan.toml` 得到，本页本身没有在本机构建、也没有在本机跑过计划。判据是 `progen check` 自己打印的整行输出。

## 我量到的

**(1) 现象（现树）。** `6218b4ee4` 上：

```
progen check: 25 programs x 41 pipelines = 1025 cells (0 located refusals), 0 seed(s) left out as known failures
41 wrong
seed 24 [macro+simplify@nvm]: RUN_ERROR: An error has occurred: Compile encode encountered an unmapped live node ref 164..
```

seed 24 共 **9** 条流水线报同一错，全部 `@nvm`：`macro+simplify`、`macro+inline`、`simplify+simplify`、`opt+opt`、`inline+opt`、`spec+inline`、`devirt+inline+dce`、`inline*3`、`macro+fold+cse+dce`。抛错点 `src/compile/gir/graph_encode.cpp:303`：`remapNodeRef` 里那个活节点在映射里落空。

**(2) 两侧各自都不报这个错。**

| 树 | 在那次 merge 里的角色 | 读数 |
|---|---|---|
| `2097d73c9` | lane 侧父，提交信息 `fix(gir): a var read in a sync body is not a statement of the chain` | `1025 wrong`（整批另有原因），seed 24 **不**报此错 |
| `9cbcfe445` | `fsh-dev` 侧父 | `223 wrong`，seed 24 **不**报此错 |
| `3de8aa858` | 两者的 merge | `164 wrong`，seed 24 报 `unmapped live node ref 165` |

**一行说明**：两侧改动**合起来**才坏——某条活节点在 `remapNodeRef` 的映射里没有落点；两侧单独都不出现这个错。

**(3) 坏点是那次 merge，不是某条提交自己带进来的。** `3de8aa858` = `Merge remote-tracking branch 'origin/fsh-dev' into lane/fault-predicate`（父 `2097d73c9` + `9cbcfe445`，2026-10-26 23:10 UTC）。再上溯一层，`bd6f0bf57` = `Merge branch 'lane/fault-predicate' of ../lanes/loops into fsh-dev`（2026-10-27 00:20 UTC）；自那次起这条线上就带这个错。

**(4) 不是我们这条线引入的。** `341518b06`（我们那三批改动之前）同一条整行同样红、同样 `41 wrong`；更早的 `e71bee236` 及以前是 `861 wrong` 且 seed 24 报 `FRONTEND: Unexpected 'sync'`（程序根本没进编译器）。**跨 `e71bee236` → `7ba667e78` 这条线不能当二分依据**：两侧读数不可比，一边是"没进编译器"，一边才是本页这个错。

**(5) 已排除的提交（逐个实测，seed 24 都不报此错）。** `f3e0594f2`、`ede52a4ac`、`e9d4dbdb1`、`82b539938`、`f114ac0b9`、`309c07cb8`、`ba2aa2d22`、`bfb7c055d`、`42c4cd39b`、`a20253e2c`、`ee8adf95c`、`60b2e4be5`、`2097d73c9`、`9cbcfe445`。

**(6) 同一次 merge 还换了生成器。** `3de8aa858` 一并带来 `test/tools/progen/model.py`（`+23/−8`）与 `test/tools/progen/generate.py`（4 行）的改动，seed 24 的程序文本也随之变了。所以"编译器缺陷"与"程序换写法之后被暴露"要分开看，别只按编译器一侧二分。

## 要问的一句

**这两侧改动（`2097d73c9` 的"`sync` 体内 `var` 读不算链上语句" 与 `9cbcfe445` 的图编码侧）合起来才产生的 `unmapped live node ref`，是你们改，还是需要我们把这两侧的相互影响也量出来（例如给出 `remapNodeRef` 在 seed 24 上遇到的节点形态）？**

## 没量到的

- 没量到 `remapNodeRef` 在 seed 24 上拿到的是哪个节点、什么形态（哪个 kind、哪条边把它留在 live 集合里）：本页只到"活节点没有映射"这一层，没有往下取图。
- 没量到 merge 两侧各自的哪个具体改动参与其中：两侧合起来是 289 个文件的改动，本页只证明了"任一侧单独都不出这个错"。
- 没量到 `41 wrong` 里其余 32 条的归因。
- 没量到这条缺陷在 `e71bee236` 之前是否也存在：那一段所有程序都进不了编译器（`FRONTEND: Unexpected 'sync'`），没有可比读数。

本页只报测量与一句询问：不改代码、计划、断言或生成物。