# 标准库重写第一批（决策者定稿）

本目录是决策者定稿的新版本：`str`、`bytes`、`re`、`json`（含 `json.signatures.cml`）、`fs`、`io`、`env`、`process`、`time`、`hash`、`array`、`statistics`。每个 `<m>.cml` 是完整的新文件，`<m>.notes.md` 写着逐函数的去留、要改的 C++、要迁移的调用处。notes 末尾的"修订"一节以它为准。`error-kinds.md` 是错误种类总表，决策者会把它并进 `docs/stdlib-conventions.cn.md`。

这些文件没有编译过（决策者这边没有构建环境）。接入要求：
1. 把各文件换进 `stdlib/`（`json.signatures.cml` 换进 `stdlib/signatures/json.cml`），按 notes 做 C++ 侧的增删改，迁移调用处。每个模块一个提交，每个提交都是绿的。
2. **编译不过或语义不对时，按草稿的意图修**：语法写错、调用写错，照本意改过来。修不了，或者要改语义（函数的去留、签名形状、错误还是故障），写进问题页问决策者，不要自己重设计。修了哪些草稿错误列进报告，决策者据此改进写法。
3. notes 里标出的风险点先编译验证：`flatten` 的两个重载在 `int[][]` 上会不会同时匹配（会的话，`T[..]` 那一个加 `when !is_array::<T>()` 区分）、`match` 作 `loop` 体、可空数组的推导式。
4. **`fail<'Kind'>(message)`**（在 Camel 函数体里主动报故障的核心原语）还在等负责人决定。`array`、`statistics` 里用到它的五处，在决定之前先不接；这两个模块的其余部分照接。
5. 已定的跨模块改动：
   - 字符串与 `Bytes` 的范围下标 `s[a:b]` 进核心，`str::slice`、`substr` 删除；
   - `len(Bytes)` 并进核心的 `len`，`Bytes + Bytes` 是拼接；
   - `sleep`、`exit` 只在 `process`，前奏里的两份删掉；
   - `sum`、`mean`、`min`、`max`、`argmax`、`argmin` 作用于 `T[..]`，归 `array`，`tensor` 的整体形式删掉；
   - 空归约是故障（设计原则 7.7），`sum` 也是；
   - `time` 只有 UTC，C++ 里写死的 8 小时偏移是缺陷，随之删掉。
6. 文本组三个小问题已定：正则方言 ECMAScript；`replace` 的替换文本不展开 `$1`（用 `replace_with`）；`bytes` 暂不加十六进制函数。
