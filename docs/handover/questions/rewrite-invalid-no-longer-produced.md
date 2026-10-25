# 问题：`RewriteInvalid` 不再由模块产生，`@fails` 还写着它

事实与读数，不含该修哪儿的判断。基座 `af4b633ba`（`origin/develop`）。

## 1. 事实

- `modules/rewrite/module.cpp` 里 `RewriteInvalid` 只由五处兜底 catch 产生（`rewrite`、`run`、`why_not`、
  `saturate`、`extract`），它们把**别人的**异常转成"规则写错了"。按"只捕获自己的规则错误"的规则，
  这五处都去掉了：留下的是 `RuleCompileError` → `RuleInvalid`、`std::invalid_argument` → `RuleTwoWay`、
  `mm::MemoryExhausted` 原样抛出。
- 于是**这个错误值种类现在在整个仓库里没有任何产生点**（`RewriteInvalid` 只剩文档里的名字）：
  `stdlib/rewrite.cml`（`rewrite`/`saturate`/`extract` 的 `@fails`）与 `stdlib/rules.cml` 的若干 `@fails`
  仍写着它，`docs/stdlib-reference.md`（生成物）也随之写着它。
- 兜底 catch 原本还覆盖一类**有名**的失败：`camel::runtime::GraphCommitConflict`
  （`src/runtime/draft_session.cpp` 的 `commitChecked`）。它现在照常往外传，pass 那一层按
  `RuntimeDiag::PassInternalError` 报（"a defect of the compiler"），不再是 `RewriteInvalid`。

## 2. 要问的

1. `@fails` 里的 `RewriteInvalid` 该删掉，还是该保留一个**明确的**转换（例如只把
   `GraphCommitConflict` 这类模块自己知道的操作失败转成它）？删它就等于承认"改写不能进行"
   这类失败以后由 pass 层报。
2. 若删：`docs/stdlib-reference.md` 是生成物，重录它算不算这次改动的一部分（本车道没有本地构建，
   只走了远端 `camel-remote.ps1`，没有在仓库里重录生成物）。

## 3. 没做到的

- 没查 `@fails` 的种类是否被某个计划或工具检查（`test/tools/check-doc-pass-forms.mjs` 只看 pass 文档形式），
  所以不知道删掉它会不会牵动别的检查。
- 没有改任何 `@fails` 文本：期望与文档一律不动。