# 标签的参数能不能是一个 `rule`：`#jvp(add_jvp)` 的实参今天被拒

挂 `#jvp` 到运算符声明上的最后一笔停在这里：`#jvp(add_jvp)` 里的 `add_jvp` 是一条 `rule`，而标签的实参今天只收"程序里的一个函数"。这不是摆放位置的问题——三种摆法都试过，结果相同。

## 一、今天的拒绝（原文）

`SemanticDiag::TagArgumentNotValue`（`0x03000202`），钉在 `test/plans/feat/compile/tags.plan.toml#err_rule_argument`：

```
The tag 'jvp' cannot take 'mul_rule' as an argument: it names a declaration of this program that is not one function.
```

用例 `test/cases/compile/tags/err_rule_argument.cml` 的注释把这条的来历写在那里：**"A rule is a declaration, and whether a tag parameter may carry one is open (`questions-2026-10-29-transforms.md` question 1), so it is refused"**——即"标签参数能否携带 `rule`"从来就没有被裁过，今天按"不能"执行。

## 二、试过的三种写法，结果都一样

在 `stdlib/prelude.cml` 里（标签、`add_tangent`/`add_jvp`、`func operator +` 同处一个模块）：

1. **把规则搬进 `prelude`**（`add_tangent` 与 `add_jvp` 随标签一起进 prelude，`autodiff` 侧删除）；
2. **写到 `#op` 同一行**：`#op(':op/add_f') #jvp(add_jvp)`，紧接 `export func operator +(x: f32, y: f32): f32`；
3. **把规则排在标签所在声明之前**（标签实参是常量，按位置读，所以先 `add_jvp` 后运算符声明）。

三次都在标签处被拒（`prelude.cml:97:19`），文字是同一族：

```
The tag 'jvp' cannot take 'add_jvp' as an argument: it names nothing this program declares. A tag is
read before the program runs, so an argument is a c…
```

与被钉的那句"not one function"是同一判据的两面：**`rule` 不是标签参数能携带的东西**，与它在哪个模块、写在声明前还是后无关。工作树已回退，没有留半成品。

## 三、它挡的是哪条裁定

17:30 的裁定：**把 `#jvp` 挂到 `operator + (f32)` 旁边**（作者原话：挂 `#jvp` 那一笔里把它们搬到 `prelude`、放在各自的 `func operator` 声明旁边）。运算符声明在 `prelude`，`#jvp` 与三条标签这一笔已经进了 `prelude`（`#prelude` 里的 `#op(':op/add_f')` 就在那里），唯一缺的就是把 `add_jvp` 这个名字写进 `#jvp(...)`——**这条裁定的最后一步正好撞在没裁的那一问上**。

## 四、两条出路

- **（a）标签参数允许是同一模块里声明的 `func`/`rule`**：放宽 `TagArgumentNotValue` 的判据，从"必须是这个程序里的一个函数值"改成"必须是这个模块声明的一个函数或一条规则"。**要多造**：判据一处 + 诊断文案（"not one function" 改为"a declaration that is neither a function nor a rule"）；**要写清**：放宽后"任意声明"（类型、枚举、结构、值）仍然被拒，判据不是"任何声明都行"。**改变语义**：不改变已有程序的含义，只让一类以前被拒的写法可用。
- **（b）规则用别的写法引用**（例如按名字字符串给 `#jvp('add_jvp')`）：**要多造**：标签实参的一种新形状 + 运行期的名字查找；**代价**：失去静态检查——名字打错在编译期不再被发现，且"标签参数是常量"这条也随之作废。

**默认建议：（a）**。理由：**标签本来就是给编译器看的，不是值**；它的实参不需要是运行期可求值的东西，只需要在编译期唯一指名一个规则。放宽到"本模块声明的函数或规则"，既不引入运行期查找，也不让类型/值这类声明混进来。

## 附一：`prelude::jvp` 这个限定名

标签进了 `prelude` 之后，别处的引用形式是**限定名**：`test/cases/modules/autodiff/transform_rules.cml` 读回规则时写 `rule_of(g, 'prelude::jvp')`，`src/core/node_attrs.cpp` 的属性命运表里三个键是 `prelude::jvp` / `prelude::transpose` / `prelude::vjp`。下一个人会问"为什么不是 `autodiff::jvp`"——因为声明它们的是 `prelude`，标签的归属模块就是它声明的模块。

## 附二：一处与本问无关的偶发

`resnet_gradient_smooth_vs_torch64` 与 `resnetbn_gradient_relu_kinks_vs_torch64` 在一次全量跑 `test/plans/feat/modules/autodiff.plan.toml` 时报过 numpy reshape / torch 数据文件读取错；两者 `tier = "slow"`，不在快闸里，复跑即过。与本问无关，未追。