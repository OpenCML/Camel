> **裁定：可以。** 作者 20:10（`6f9b0886b`）：标签实参可以是一个函数，或一条 `rule` 声明；类型、枚举、
> 结构、值仍然拒绝，诊断文案照我们写的改；`rule` 名按普通的作用域和引入规则查找，不限"同一模块"，
> 只要按引入规则看得见就行。落地：`00d14e76f`（一接四拒，闸 7956/7946/0/8/2）。页不删，留着：
> 下一个人要知道这条路为什么长这样。

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

## 为什么装名字、以及名字从哪来

裁定之后，这一笔的形状由代码定，不由偏好定：**`rule` 声明被降成 `let <name> = __rule(...)`
（实例化时 `__rule_generic(...)`），所以它的名字是一个 `Rule` 类型的值**——没有图代表它，它只出现在值
命名空间里。于是：

- **判据是"初始化表达式调的是哪个制作者"**（`__rule` / `__rule_generic`），落在
  `src/compile/gir/builder_tags.cpp#declarationOf`；这与 `src/compile/gir/builder_operators.cpp` 里按
  `ops->name() == "__rule_generic"` 认同一件事的办法是同一个。
- **不能在图那一层判**：到那里只剩图与符号，而普通 `rule` 两样都不是——`genericRuleTable` 只登记
  `registerGenericRule` 过的条目，注释写明 "An ordinary rule's value keeps 0, so no rule is ever
  registered under it"。
- **不能嗅类型串**：在值声明节点上读类型拼写会让编译器**崩溃**（接受用例与拒绝用例双双 `exit null`，
  2026-10-31 实测）。那一次的注释留在 `declarationOf` 上方。
- **装的名字是"符号"**（`camel::core::attr::symbol`），读者按名字在模块的规则里查——名字是它唯一诚实的表示。

## 三个上下文的读数（2026-10-31，`00d14e76f`）

| 上下文 | 读数 |
|---|---|
| 普通模块，标签在 `rule` 声明之前 | `6.0`，编译通过 |
| 普通模块，标签在 `rule` 声明之后 | `6.0`，编译通过 |
| `import` 进来的模块（`import { mul } from geo`，`geo` 里既有 `rule` 也有 `#jvp(mul_rule)`） | `6.0`，编译通过 |
| `prelude`，同一文件里加 `rule` | **不可能**：`SemanticDiag::ImportedModuleHasErrors`，`Cannot import module 'rewrite': it has errors` |

**所以"解析顺序缺陷"这条不成立**：同一个模块里，标签写在 `rule` 前或后都通；引入进来的模块也通。`prelude`
那次报 "names nothing this program declares" 是**正确**的读数——`add_jvp` 在 `autodiff` 里，`prelude`
按分层看不见它，不是顺序问题。

## 附：`prelude` 里放不了 `rule`（这一条挡着"挂到 `operator + (f32)` 旁边"）

在同一条链上还量到一件更硬的事：**`prelude` 的声明阶段连 `import { Node } from graph` 都做不到**
（`ImportedModuleHasErrors: Cannot import module 'graph': it has errors`），只能 `import` 纯声明的
`types`。而 `rule` 的降级需要 `import { __rule } from rewrite`，`rewrite` 又导入 `graph`。
所以 **`#jvp(add_jvp)` 挂在 `prelude` 的 `operator + (f32)` 旁边，今天是做不到的**——不是判据问题、
不是摆放位置问题，是"`prelude` 的声明阶段不能引入有值的模块"这条边界。要给 `#jvp` 一族一个挂点，
得选一个既能看见运算符声明、又能看见 `autodiff` 的位置；那是作者的新裁定，不是施工方能定的事。
