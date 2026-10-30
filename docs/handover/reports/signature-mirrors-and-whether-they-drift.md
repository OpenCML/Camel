# `stdlib/signatures/*.cml` 漂了没有：75 条，零过时、零漏登记

日期：2026-10-31　车道：`lane-recheck`

## 这一族文件是什么

**不参与编译的镜像。** `stdlib/signatures/{autodiff,ckpt,nn,tensor,tiles}.cml` 五个文件，喂
`docs/stdlib-reference.md` 与 `--verify-ops` 的检查；**没有任何编译路径读它们**。它们自己的头写着
"for reference only (it is not compiled)"，`docs/design/stdlib/README.cn.md:11` 也写明"签名文件只覆盖 18 个
模块，且不参与编译"。

真正的可加载声明在 `stdlib/<模块>.cml`（`#op('<模块>:<算子>')` + 无函数体的 `func`），配合
`modules/<模块>/module.cpp` 的 `kDeclaredOperators`（已声明的名字不再由 C++ 导出）。**算不出签名的算子**
（元素类型或形状随实参走）留在 C++，只在镜像里登记。

## 比对结果（2026-10-31，源文本比对，不需要构建）

| 模块 | 镜像条目 | 一致 | 文件里有、真声明里没有 | 真声明里有、文件里没有 |
|---|---|---|---|---|
| `autodiff` | 4 | 4 | 0 | 0 |
| `ckpt` | 1 | 1 | 0 | 0 |
| `nn` | 20 | 20 | 0 | 0 |
| `tensor` | 43 | 43 | 0 | 0 |
| `tiles` | 7 | 7 | 0 | 0 |
| **合计** | **75** | **75** | **0** | **0** |

**两个总数：过时 0 条，漏登记 0 条。**

## 这次量的是什么，没量什么（重要）

- **量了**：镜像里每一条 `func <名字>`，(a) **没有**出现在 `stdlib/<模块>.cml` 的 `#op` 声明里——即镜像没有
  登记一个"其实已经声明化"的名字；(b) 这个名字在 `modules/<模块>` 的 C++ 源里出现——即镜像没有登记一个
  模块里根本不存在的名字。
- **没量（这张表本身没量）**：① 签名里写的**参数与返回类型逐字是否等于**算子注册的类型；② **反方向的完整性**
  （某个算子既没声明、也没登记进镜像）。这两样要算子表。

## 那张表之外：`--verify-ops` 真的跑起来了，550 条

镜像与算子表的一致性由 `scripts/stdlib-reference.mjs --verify-ops` 比，它在**快速闸里本来就在跑**，名字是
`registry.signatures_match`，这一晚每次全量闸都是 `[PASS]`（它比的是：**表中每个注册的重载都被声明、每条声明
都对着一个注册的重载、参数个数一致、`@pure`/`@effect` 与声明的纯度一致、签名文件的条目不带函数体**）。手工跑一次
的读数：

```
$ CAMEL_TEST_EXTRA_LIB_DIRS=$HOME/camel-eval/conda/envs/clang/lib \
    node scripts/stdlib-reference.mjs --verify-ops
the declarations agree with the 550 registered operator overloads
```

**550 条重载，全部一致。** 所以第 34–35 行说的"没量"只是**这张表**没量类型逐字；镜像与算子表的一致性今天由闸守着。

**为什么手工跑会失败、以及修的是什么。** 手工跑时报 `GLIBCXX_3.4.35/3.4.36 not found`：脚本拉起 `camel` 时
只传了 `CAMEL_HOME`，没有把"安装需要更新的 `libstdc++`"这件事传下去；而闸里的 `test.js` 会给子进程加上
`CAMEL_TEST_EXTRA_LIB_DIRS` 里的库目录，所以同一个二进制在闸里跑得动、手工跑不动。修的是**启动器传环境**这一段：
`scripts/stdlib-reference.mjs#installEnvironment` 现在把 `CAMEL_TEST_EXTRA_LIB_DIRS`（`:` 分隔）加到子进程的
`LD_LIBRARY_PATH` 前面，`--verify-ops` 与文档示例两个 spawn 都走它。对照读数（同一条命令、去掉变量）：

```
$ env -u LD_LIBRARY_PATH -u CAMEL_TEST_EXTRA_LIB_DIRS node scripts/stdlib-reference.mjs --verify-ops
camel inspect --ops failed (exit 1): ...GLIBCXX_3.4.36 not found...
```

## 判据（写下来，免得下次重新想）

> **一个不参与编译的镜像文件，唯一的风险是漂移；它今天可信，不等于明天可信。** 要么每次声明化之后比一次，
> 要么让它从真声明生成。

> **一项检查跑不起来时，正确的状态是红，不是跳过**——跳过会让人以为它通过了。

**今晚这条检查没有被当成通过记着**（它在闸里是真 `PASS`，不是 `skip`，也不是 `known failure`）；出问题的是
"手工跑它"这条路：它给出的是一句看起来像"这项检查坏了"的加载器报错，而那是**环境没传下去**——这正是"跑不起来"
最容易被读成"跳过"的地方，所以修的是环境传递，不是检查本身。

**值得做的方案（这一轮不做）**：把镜像改成**生成物**——从 `stdlib/<模块>.cml` 的 `#op` 声明与 C++ 算子表
（`--verify-ops` 已经有那张表的读法）生成 `stdlib/signatures/<模块>.cml`，并把它加进"生成物是一组"那套
检查（`node scripts/stdlib-reference.mjs --write` 一并写，`--check` 一并验）。这样"过时/漏登记"从"要人去比"
变成"构建时自证"。

## 第四处"文件性质与名字不符"

今晚已经两处：`semantic.cpp` 是从 TSV 反写的生成物；`program-transforms` 的设计稿只有中文、裁定要对齐的
英文版不存在。**这是第三处**：**`stdlib/signatures/` 叫"签名"，放在 `stdlib/` 下、长得像声明、`#op` 与
`func` 的写法也和声明一样，实际只喂文档与校验器**——改它不改变任何行为，不改它也不会让任何测试变红
（除非那份文档或那次检查被跑到）。