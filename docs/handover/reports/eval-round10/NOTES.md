# NOTES：第 3 项读数是怎么来的

配套正文：`item3-canonical-graph-size.md`。本页只记**方法、原始输出、构建标识**，不重复表格。

## 1. 用的是什么构建

| 项 | 值 | 从哪读的 |
| --- | --- | --- |
| 树 | `D:\Projects\Camel-wt\eval`，分支 `lane-eval` | `git rev-parse HEAD` / `git status --short`（空） |
| 提交 | `2d7c5c3a146d0572670def9370a07d5ebe71d38a` | 同上 |
| 构建所在 | 宿主 `88zjwei` 的 `~/camel-work/eval`（`camel-remote.ps1` 的转发 checkout） | `ssh 88zjwei 'git -C ~/camel-work/eval rev-parse HEAD'` |
| 配置 | Release | `out/latest/build-info.json` 的 `config` |
| 版本串 | `Camel v0.2.45 (Build 261119_051158_2d7c5c3a14)` | `camel --version` |
| footprint | `261119_051158_2d7c5c3a14` | `build-info.json` |
| revision / dirty | `2d7c5c3a14…` / `false`（`dirtyCount: 0`） | `build-info.json` |
| 源指纹 | `0244fca0cbacd5c78253c81a43e1e0f98f1c6cf5ac99097aee774fbbdbaf2c37`（1344 个文件） | `build-info.json.fingerprint` |
| 构建选项 | `09afb0dfe7d8…`：`ENABLE_FASTVM_JIT=ON`、`CAMEL_ENABLE_PYTHON=ON`、`CAMEL_TENSOR_BLAS=ON`、`CAMEL_TARGET_ARCH=native`、平台 `linux/x64` | `build-info.json.buildOptions` |
| 编译器 | Clang 23.1.2（宿主 `~/camel-work/bin/clang`，CMake 探测） | 构建日志 `-- The CXX compiler identification is Clang 23.1.2` |
| 构建时刻 | `2026-11-19T05:12:03Z` | `build-info.json.builtAt` |
| 构建结论 | `BUILD_EXIT=0`，`[DONE] Release build completed` | 构建日志 `D:\Temp\eval-round10-build.log` |

**本机没有构建任何东西**：本机不编译 Camel（会蓝屏），构建与运行都在宿主上，入口是
`internal/tools/camel-remote.ps1 -Path D:\Projects\Camel-wt\eval`（不带 `-Plan`，只构建；`-Jobs`/六计划与本次读数无关，
本项不跑计划，所以没有 `[SUMMARY]`）。

## 2. 三个数字各自的定义与数法

命令（每个文件一次，标准输出重定向到文件，在宿主上执行）：

```
cd ~/camel-work/eval
export CAMEL_HOME="$PWD/out/latest"; export PATH="$CAMEL_HOME/bin:$PATH"
camel <file> std::jgir canonical=true > /tmp/<name>.json
```

`std::jgir` 是转译遍：`apply()` 里 `out << dumpJsonRuntimeGraph(...) << '\n'` 然后返回空图
（`src/passes/trans/json/gir.cpp#JsonGirDumpPass::apply`），所以 **stdout 里只有这一份文档 + 一个换行**，
程序本身不执行。`canonical=true` 走 `dumpCanonicalRuntimeGraph`（`src/passes/trans/json/gir_canonical.cpp`），
文档顶层键是 `canonical, decls, entry, format, graphs, root, tags, types, version`（`version: 2`）。

- **节点数** = `sum(len(g["nodes"]) for g in doc["graphs"])`。跨文档全部可达图求和（`g0` 根 + 入口 + 每个被调图）。
- **边数** = `sum(len(n["inputs"]) for g in doc["graphs"] for n in g["nodes"])`。
  **规范文档没有 `edges` 数组**：每个节点把自己的输入按 `{"kind","slot","source"}` 写进 `inputs`，一条引用就是一条边。
  这是本页对"边数"的定义，也是唯一能与 `README.md` 的 93 对上的数法（见 §5）。
  `kind` 的分布（`train.cml`）：`normal 53 / context 16 / control 13 / sync 10 / order 1`。
- **字节数** = 那个重定向文件的字节数，即 `dump(2)` 的输出加结尾一个 `\n`（`wc -c`）。
  不是源文件大小，不是磁盘上 JSON 重新序列化后的大小。

## 3. 原始输出（宿主上一次跑完的整张表，未编辑）

```
file	graphs	nodes	edges	regions	bytes	sha256	kinds
examples/case-study/train.cml	5	63	93	2	29945	f3978b42249f6224	context:16,control:13,normal:53,order:1,sync:10
examples/case-study/writings/renamed.cml	5	63	93	2	29945	f3978b42249f6224	context:16,control:13,normal:53,order:1,sync:10
examples/case-study/writings/swapped.cml	5	63	93	2	29945	f2fa36d4f1a20af8	context:16,control:13,normal:53,order:1,sync:10
examples/case-study/writings/moved.cml	5	63	94	2	30060	66138fb5950779fd	context:16,control:13,normal:53,order:2,sync:10
test/cases/compile/var/step_var.cml	3	23	29	0	9183	b75fd6d66d7b3909	context:3,control:3,normal:20,sync:3
test/cases/compile/var/step_let.cml	3	23	29	0	9183	b75fd6d66d7b3909	context:3,control:3,normal:20,sync:3
test/cases/compile/var/loop_var.cml	3	24	23	2	9801	24eaf169d3619272	context:6,control:2,normal:13,sync:2
test/cases/compile/var/loop_let.cml	3	24	23	2	9801	24eaf169d3619272	context:6,control:2,normal:13,sync:2
test/cases/compile/var/arm_var.cml	3	13	13	2	5418	838e74d0cb36fdc9	context:3,control:2,normal:6,sync:2
test/cases/compile/var/arm_let.cml	3	13	13	2	5418	838e74d0cb36fdc9	context:3,control:2,normal:6,sync:2
test/cases/compile/chain/pure_call_first.cml	3	10	8	0	3947	ac891942c54a8cf0	context:1,control:1,normal:5,sync:1
test/cases/compile/chain/pure_call_second.cml	3	10	8	0	3947	ac891942c54a8cf0	context:1,control:1,normal:5,sync:1
test/cases/compile/chain/effect_call_first.cml	3	8	9	0	3677	28a4f2a28736c649	context:1,control:2,normal:3,order:1,sync:2
test/cases/compile/chain/effect_call_second.cml	3	8	9	0	3677	27d7dea24c8391e7	context:1,control:2,normal:3,order:1,sync:2
test/cases/compile/chain/fault_call_first.cml	3	10	10	0	4178	478f1af0ceac50e8	context:1,control:2,normal:5,sync:2
test/cases/compile/chain/fault_call_second.cml	3	10	10	0	4178	e2e497783b43c97c	context:1,control:2,normal:5,sync:2
```

`regions` 列不是本项要求的，顺带记的（`train.cml` 的 `g4` 两个区域）。

逐图分解（同一命令，`graphs[*]` 逐个求和）：

```
examples/case-study/train.cml            | g0=2n/1e/0r g1=26n/50e/0r g2=14n/15e/0r g3=8n/8e/0r g4=13n/19e/2r | 63 nodes 93 edges
examples/case-study/writings/renamed.cml | g0=2n/1e/0r g1=26n/50e/0r g2=14n/15e/0r g3=8n/8e/0r g4=13n/19e/2r | 63 nodes 93 edges
examples/case-study/writings/swapped.cml | g0=2n/1e/0r g1=26n/50e/0r g2=14n/15e/0r g3=8n/8e/0r g4=13n/19e/2r | 63 nodes 93 edges
examples/case-study/writings/moved.cml   | g0=2n/1e/0r g1=26n/51e/0r g2=14n/15e/0r g3=8n/8e/0r g4=13n/19e/2r | 63 nodes 94 edges
```

## 4. 取数脚本（宿主上原样放 `/tmp`，不进仓库）

```python
import hashlib, json, subprocess, sys

for f in sys.argv[1:]:
    p = subprocess.run(["./out/latest/bin/camel.real", f, "std::jgir", "canonical=true"],
                       capture_output=True)
    if p.returncode != 0:
        print(f, "ERROR", p.returncode, p.stderr.decode("utf-8", "replace")[:300]); continue
    out = p.stdout
    d = json.loads(out.decode("utf-8"))
    nodes = sum(len(g.get("nodes", [])) for g in d["graphs"])
    edges = sum(len(n.get("inputs", [])) for g in d["graphs"] for n in g["nodes"])
    regions = sum(len(g.get("regions", [])) for g in d["graphs"])
    print(f, len(d["graphs"]), nodes, edges, regions, len(out),
          hashlib.sha256(out).hexdigest()[:16])
```

跑法：`python3 /tmp/canon-size.py <文件...>`（宿主 `python3` 3.10）。
最后一步的 `len(out)` 就是字节数，与 `wc -c` 重定向文件一致（两个都核过：`train.cml` 29945）。

## 5. 怎么确认数法没量错

1. **与仓库里已有的独立读数对上**：`train.cml` 量出 **63 节点 / 93 边**，`examples/case-study/README.md`
   的 "Both documents hold 63 nodes and 93 edges" 一字不差；`moved` 的 **94** 边也对上它记的
   "63 nodes on both sides but 94 edges against 93"。逐图分解（2/14/8/13/26）与 README "Reading 1" 的图规模清单一致。
2. **`renamed` 的 sha256 与 `train.cml` 相同**：改名确实不改变量字节，说明量的是文档而不是文件名/路径。
3. **重跑稳定**：同一文件两次导出同为 29945 字节、同 sha256。
4. 若按错的数法（找 `graphs[*].edges`）会得到 **0 边**——规范文档没有那个数组。本页的边数是
   节点输入引用的条数；这是唯一能让 63 节点得到 93 边的数法。

## 6. 量不到的、以及本页不用的数

- **"案例程序的六个写法"量不到**：本树只有四个（`train.cml` + `writings/{renamed,swapped,moved}.cml`）。
  需求原文在归档仓库 `ECOOP2027/handoff/eval-round10-request.md`，本机不存在该仓库（`D:\Projects\Camel-Archive` 无；
  全盘搜 `eval-round10*`、`case-study-request*` 无命中），全树也没有任何文档点名第六个。
  所以本页给的是**树里全部 16 个成对写法文件**（8 组），按组列清楚，不替作者选哪六个。
- **`README.md` 的 29941 字节不复用**：那是 pin `96a27e435` 的旧读数，与今天同一个程序差 4 字节
  （29945）。差在哪没查（不在第 3 项范围）；按作者"不用旧数据"的要求，本页只写今天量到的 29945。
- **`README.md` 的 `step_var 10930 / step_let 9179`（red）在 `2d7c5c3a1` 上已不成立**：两文件同 9183 字节、
  同 sha256。这是 `var` 化成 SSA 落地后的状态，本页按今天量到的写。
- 本项**没有**跑任何计划（六计划与本项无关），因此没有 `[SUMMARY]`，也没有 `pass/fail` 计数可报。

## 7. 复现（从本机，两步）

```powershell
# 1) 在宿主上建 Release（本机不构建）
pwsh -NoProfile -File D:\Projects\Camel-wt\eval\internal\tools\camel-remote.ps1 -Path D:\Projects\Camel-wt\eval

# 2) 在宿主上导出规范图（示例一条）
ssh 88zjwei 'cd ~/camel-work/eval && CAMEL_HOME=$PWD/out/latest PATH=$PWD/out/latest/bin:$PATH ./out/latest/bin/camel.real examples/case-study/train.cml std::jgir canonical=true | wc -c'
```