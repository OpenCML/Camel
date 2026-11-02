# 抬升列表已定为 grouped、空切向占位；剩下的是建图里的端口次序

**结论（作者已裁）。** `jvp` 抬升后的参数列表是 **grouped**：先全部原值、再全部切向；没有切向空间的参数，切向那一位是 `()`。`transpose` 的准确形状是 `(ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)` —— 那些"调用方还要传的值参数"不存在，它们是**捕获**，进闭包。**不加任何新的类型操作**：交错是**建图**的错，不是声明缺一条运算。

规则落在声明上（`stdlib/types.cml#Jvp<F>`、`#Transpose<F>` 就是那两行），理由不写进文件、写进提交信息。

## 这一轮量到了什么

按 grouped 改了 `modules/transforms/jvp.cpp#jvpSignature`（先值后切向，空位 `Type::Void()`），并把 `modules/transforms/split.cpp#valuePortsOf` / `#parameterOfPort` 改成按中点两分读：

| 量的是 | 读数 |
| --- | --- |
| `test/plans/feat/compile/transforms.plan.toml` 失败数 | **13 → 14**，新增的是 `grad_of_a_division.nvm` |
| `vjp(f)([1.0,2.0],[3],[4])`（`f(m: float[..], a: int[..], b: int[..])`） | 仍报，但**换了一层**：被调者的类型印成 `(float[..], int[..], void) => (float, (int[..], float[..], void) => float pure) pure` |

第二条是这一页存在的理由：**签名改了，图的端口没改**。`split` 现在按"前一半是值、后一半是切向"切，可 `jvp` 建出来的成员仍然按 `(v1, d1, …, vn, dn)` 排端口（`modules/transforms/jvp.cpp`，"signature's 2n parameters are these 2n ports, in the same order" 那一段），于是 `split` 把一个切向当成了值 —— 印出来的 `(float[..], int[..], void)` 就是"值一栏里混进了 `void`"的样子。

所以作者那句"交错是建图的错"指的就是这处：**要改的是端口与参数的对应关系，不是再补一条类型运算**。改完 `jvpSignature` 只是让**声明**对上了，`split` 要的那一半还得让**图**也按同一顺序排。

## 下一步（未做，按此顺序）

1. `modules/transforms/jvp.cpp`：建图时按 grouped 排端口 —— 值端口 0..n-1，切向端口 n..2n-1；空切向那一位既占端口，又恒为零向量（它对应的不是任何值）。
2. `modules/transforms/transpose.cpp#transposeSignature`：按 `(ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)` 建，值参数从闭包里取。
3. `stdlib/signatures/transforms.cml#split` 的声明改成 `Split<F>`（`stdlib/types.cml` 同一次加），并把两处"等展开有能力再改声明"的话删掉。
4. 量：`vjp(f)([1.0,2.0],[3],[4])` ⇒ `∂f/∂m[0] = a[0] = 3`，应得 **3.0**；旧的 `vjp(two)`（两个 float，余切 2.0 / 3.0）不许变；showcase 19/19；全量新增红逐名。

## 为什么上表那两步要一起改

只改签名不改图，`split` 切错位置、`grad_of_a_division` 变红 —— 这就是上表第一行那一条新增红。一次改到图端口，两个读数会同时动；分两次提交会让中间那次落在"声明对了、实现还按旧序读"的状态上，正是作者点名的那类不一致。

## 我方默认

**默认按页里那四步的顺序做完：先在图端口上按 grouped 排列，再按声明建 `transposeSignature`，最后才改 `stdlib` 的声明。**

理由就是页里那句"只在签名上改，`split` 会切错位置"：三条读数是同一次改动里的三个片面，**分两次提交的中间态就是作者点名的那类不一致**（声明对了、实现还按旧序读）。所以默认不是"先让签名过"，而是"让图和签名同时到位，中间不留一个自洽不了的落点"。

若作者裁定只保留一种排布（grouped 或 interleaved），则这一页退为记录那两种排布各自的读数，工作量减半。
