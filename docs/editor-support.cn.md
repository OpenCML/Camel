# 编辑器支持

中文简体 | [English](editor-support.en.md)

Camel 的编辑器支持由两部分组成：

- **`camel-ls`**：语言服务器，通过标准输入输出使用[语言服务器协议（LSP）](https://microsoft.github.io/language-server-protocol/)。它随仓库的其余部分一起构建，任何带 LSP 客户端的编辑器都能使用。
- **VS Code 扩展**，位于 [`editors/vscode`](../editors/vscode)：`.cml` 文件的 TextMate 语法与语言配置、一个启动 `camel-ls` 的薄客户端，以及协议中没有对应物的两条命令（显示函数的图、让文件经过一个遍序列运行）。

调试是第三部分，另有专文：[调试 Camel 程序](debugging.cn.md)。扩展贡献了调试器类型 `camel`，它启动 `camel-db --dap`。

---

## 1. 构建与安装

### 1.1 `camel-ls`

`camel-ls` 是 `tools/` 下的工具之一（`tools/lang-server`），普通构建会把它与 `camel` 一同产出：

```bash
npm run build
```

可执行文件是 `out/latest/bin/camel-ls`（Windows 上为 `camel-ls.exe`）。让 `CAMEL_HOME` 指向安装根目录并把其中的 `bin` 加入 `PATH`，编辑器最容易找到它：

```powershell
$env:CAMEL_HOME = "project-root\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
```

```bash
export CAMEL_HOME=/path/to/project-root/out/latest
export PATH="$CAMEL_HOME/bin:$PATH"
```

服务器以 `camel-ls --stdio` 启动，不需要其他参数。与 `camel` 一样，它通过 `CAMEL_HOME`（或 `CAMEL_STD_LIB`）找到标准库。

### 1.2 VS Code 扩展

要求：Node.js 20 或更高版本，VS Code 1.82 或更高版本。

```bash
cd editors/vscode
npm install
npm run package
code --install-extension camel-lang-0.1.0.vsix
```

`npm run package` 对照词法文法检查语法文件、编译 TypeScript 源码，并写出 `camel-lang-<版本>.vsix`。不安装而试用扩展：执行 `npm install` 与 `npm run compile` 后在 VS Code 中打开 `editors/vscode`，按 F5。

扩展按以下顺序查找 `camel-ls`：设置项 `camel.server.path`，然后是 `$CAMEL_HOME/bin`，最后是 `PATH`。`camel`（用于运行命令）以同样的方式查找，设置项为 `camel.path`。找不到 `camel-ls` 时，扩展会报告它查找过的位置并指向该设置项；语法高亮不依赖服务器。

| 命令 | 作用 |
|------|------|
| `Camel: Restart Language Server` | 停止 `camel-ls` 并重新启动。 |
| `Camel: Show Graph of Current Function` | 把光标所在函数的图以 Graphviz DOT 文本显示在编辑器旁的只读文档中；保存文件时刷新。可用 Graphviz 预览扩展渲染。 |
| `Camel: Run Current File with Passes…` | 询问一个遍序列（例如 `std::opt::inline std::fvm`），在集成终端中运行 `camel <文件> <遍>`。 |

| 设置项 | 默认值 | 含义 |
|--------|--------|------|
| `camel.server.path` | `""` | `camel-ls` 的路径。 |
| `camel.path` | `""` | `camel` 的路径。 |
| `camel.graph.passes` | `[]` | 打印图之前施加的遍，例如 `["std::opt::inline"]`。 |
| `camel.run.passes` | `["std::opt::inline", "std::fvm"]` | 第一次运行文件时建议的遍序列。 |
| `camel.trace.server` | `off` | 记录协议消息（`messages` 或 `verbose`）。 |

详见 [`editors/vscode/README.md`](../editors/vscode/README.md)。

---

## 2. 能力


| 能力 | LSP 方法 | 状态 |
|------|----------|------|
| 诊断（推送） | `textDocument/publishDiagnostics` | 已实现。文档打开或变更后发布；只对没有声明拉取式诊断的客户端推送（否则每个问题会显示两次），可用 `initializationOptions.diagnostics` = `"push"`、`"pull"`、`"both"` 覆盖。 |
| 诊断（拉取） | `textDocument/diagnostic` | 已实现。`data` 携带目录名、建议、附注，以及编译器自己给出的改法（`data.fixes`：一个标题，加若干 `{range, newText}` 编辑），次要位置作为 `relatedInformation`。 |
| 增量同步 | `textDocument/didChange`（增量） | 已实现（区间编辑，UTF-16 位置）。 |
| 悬停 | `textDocument/hover` | 已实现：按源码写法显示声明与类型、声明上方的注释、泛型函数的各实例以及调用处选中的实例或重载、字段、字面量、库函数签名、光标处诊断码的 `--explain` 说明。命名空间的成员按完整名字显示，如 `func Tools::inc(x: int): int`（设计原则 10.2）；大纲与补全同样如此。 |
| 跳转到定义 | `textDocument/definition` | 已实现：局部变量、参数、函数（调用选中的重载）、类型别名、类型变量、跨文件的导入名。用 C++ 实现的库函数没有定义位置。 |
| 查找引用 | `textDocument/references` | 已实现，范围是一个程序：打开的文件与它导入的模块。 |
| 文档高亮 | `textDocument/documentHighlight` | 已实现。 |
| 文档符号 | `textDocument/documentSymbol` | 已实现（层级结构；有语法错误的文件也可用）。 |
| 工作区符号 | `workspace/symbol` | 已实现：已打开的文档与工作区目录下的 `.cml` 文件（只解析，不编译）。 |
| 补全 | `textDocument/completion` | 已实现：作用域内的名字、`.` 之后的字段、`::` 之后的模块成员、关键字、带签名的内建与库函数；文本暂时无法解析时仍可用。命名空间成员的标签是完整名字（`Tools::inc`），插入的是该处要写的部分（`inc`）。 |
| 签名帮助 | `textDocument/signatureHelp` | 已实现，针对按名字调用的函数；不支持 `f<...>(`，不支持没有声明类型的函数值。 |
| 重命名 | `textDocument/rename` | 已实现，带 `prepareRename`；只要可能漏掉引用就拒绝并说明原因（见已知限制）。 |
| 语义标记 | `textDocument/semanticTokens/full` | 已实现（`full`）。 |
| 格式化 | `textDocument/formatting` | 已实现（整份文档；有语法错误的文档不格式化）。 |
| 范围格式化 | `textDocument/rangeFormatting` | 未实现：格式化器（`tools/format`）只支持整份文件。 |
| 代码操作 | `textDocument/codeAction` | 已实现。请求范围触及的每条诊断所带的改法都作为 quick fix 给出，其工作区编辑就是那几处编辑，并且该操作会引上它修的那条诊断；解析不到的名字由“作用域内找那个本来想写的名字”的候选搜索来修（设计原则 4.1），并标为 preferred。不带改法的诊断什么都不给：它们的 `suggestion` 是文字，编辑器会直接应用的猜测比一句话更糟。 |
| 函数的图 | `camel/graph`（自定义） | 已实现：光标所在函数的 `std::rgir` 文本，可先运行指定的遍序列，使用未保存的文本。 |

### 2.1 语义标记

服务器的图例使用标准的标记类型（`namespace`、`type`、`typeParameter`、`parameter`、`variable`、`property`、`function`、`macro`、`keyword`、`operator`、`number`、`string`）和以下修饰符：

| 修饰符 | 标准 | 含义 |
|--------|------|------|
| `declaration` | 是 | 该标记声明了这个名字。 |
| `readonly` | 是 | 不可变绑定（`let`）。 |
| `defaultLibrary` | 是 | 标准库中的名字或内建名字。 |
| `mutable` | 否 | 用 `var` 声明的变量或参数。 |
| `sync` | 否 | 声明为 `sync func` 的函数：它有副作用。 |

`mutable` 与 `sync` 是阅读 Camel 代码时最要紧的两个区分：一个名字能否被重新赋值，一次调用是否有副作用。VS Code 扩展声明了这两个修饰符并给出默认样式（`mutable` 加下划线，`sync` 用斜体）。在其他编辑器中，请把它们映射到你选定的高亮；不认识某个修饰符的编辑器会忽略它。

### 2.2 `camel/graph` 请求

自定义请求，由客户端发往服务器，以文本形式返回某个位置所在函数的图。

参数：

```json
{
  "textDocument": { "uri": "file:///path/to/file.cml" },
  "position": { "line": 7, "character": 10 },
  "passes": ["std::opt::inline"]
}
```

`passes` 可省略：打印图之前按顺序施加的遍。

结果：

```json
{
  "function": "fib",
  "passes": ["std::opt::inline"],
  "text": "digraph GraphIR { ... }",
  "diagnostics": ""
}
```

`function` 是该位置所在函数的名字，不在任何函数内时为 `null`；`text` 是 Graphviz DOT，格式与 `std::rgir` 遍的输出相同（见 [cli.cn.md](cli.cn.md)）；`diagnostics` 若存在，是编译器在构建图时报告的内容。

---

## 3. 已知限制

以下各项均在容器中用 LSP 测试驱动观察得到。**没有任何一项在编辑器中验证过**：VS Code 扩展能编译、能打包，
但激活、配色、看图视图与运行命令都没有在 VS Code 里实际运行过。

- **整份重编。** 没有增量编译。请求需要模型时，或最后一条消息之后 150 毫秒（`initializationOptions.debounceMs`）
  重建模型。2000 行的文件约 60 毫秒，20000 行约 1 秒（见
  [technical/32_semantic_query_layer.md](technical/32_semantic_query_layer.md) 第 5 节）。
- **导入的模块从磁盘读取。** 被导入文件中未保存的修改，对导入它的文件不可见。
- **一次只看一个程序。** 引用、重命名与高亮的范围是打开的文件及其导入的模块。谁导入了某个文件是未知的：
  在库文件里查引用看不到它的使用者，导出的名字不能重命名。
- **重命名在不确定时拒绝**：文件有错误、名字被导出、`main`、名字声明在另一个文件、字段、库函数、
  名字在从未被调用的泛型函数里有使用、新名字在旧名字可见的范围内已被声明或使用。
- **有错误的文本。** 有语法错误时，大纲、局部定义与补全仍然可用（基于解析器恢复出的语法树）；类型取自上一份能编译的文本，
  因此此后新增声明的类型要到文本重新能编译时才知道。一个文件只报告第一个语法错误（编译器的选择）。
- **字段**按接收者类型的文本匹配；对字段跳转到定义没有结果。
- **签名提示**由光标前的记号推出：`f<...>(...)` 的 with 实参里不支持，调用与光标之间出现块 `{` 时停止查找。
- **语义高亮**跳过跨多行的记号（多行字符串、块注释），由 TextMate 文法着色。
- **`camel/graph`** 用 `camel-ls` 旁边的 `camel` 可执行文件处理文本的临时副本，副本写在文档旁边（以便相对导入能解析），
  用后删除；目录只读时请求失败。一个函数的图包含嵌套在其簇内的函数。
- **非 UTF-8 文本**无法发送（协议是 JSON）；这样的帧被丢弃并回复解析错误。
- **文档注释**是服务器的约定，不是语言的约定：声明正上方的 `//` 行（或一个块注释）。

---

## 4. 其他编辑器

`camel-ls` 是普通的 stdio 语言服务器，任何 LSP 客户端都能启动它。下面的配置**未经验证**：它们凭对各编辑器配置接口的记忆写成，既没有对照其文档核对，也没有在编辑器里运行，更没有实际对接 `camel-ls`。配置假定 `camel-ls` 在 `PATH` 中，否则请写出完整路径。

这些编辑器都没有 Camel 的语法定义。高亮只能来自服务器的语义标记（在编辑器支持语义标记的前提下）。

### 4.1 Neovim（内置 LSP 客户端）

Neovim 0.11 或更高版本，写在 `init.lua` 中：

```lua
vim.filetype.add({ extension = { cml = 'camel' } })

vim.lsp.config('camel_ls', {
  cmd = { 'camel-ls', '--stdio' },
  filetypes = { 'camel' },
  root_markers = { '.git' },
})
vim.lsp.enable('camel_ls')

vim.api.nvim_create_autocmd('FileType', {
  pattern = 'camel',
  callback = function()
    vim.bo.commentstring = '// %s'
  end,
})
```

Neovim 0.8 至 0.10 没有 `vim.lsp.config`，在文件类型自动命令中启动服务器：

```lua
vim.filetype.add({ extension = { cml = 'camel' } })

vim.api.nvim_create_autocmd('FileType', {
  pattern = 'camel',
  callback = function(args)
    vim.bo[args.buf].commentstring = '// %s'
    vim.lsp.start({
      name = 'camel-ls',
      cmd = { 'camel-ls', '--stdio' },
      root_dir = vim.fs.dirname(vim.fs.find({ '.git' }, { upward = true, path = vim.api.nvim_buf_get_name(args.buf) })[1]),
    })
  end,
})
```

图请求可以手工发送：

```lua
local params = vim.lsp.util.make_position_params(0, 'utf-16')
vim.lsp.buf_request(0, 'camel/graph', params, function(err, result)
  if result then print(result.text) end
end)
```

### 4.2 Emacs

目前还没有 Camel 的主模式；一个最小的主模式就足以让 LSP 客户端挂接：

```elisp
(define-derived-mode camel-mode prog-mode "Camel"
  "Major mode for Camel source files."
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (modify-syntax-entry ?/ ". 124b" camel-mode-syntax-table)
  (modify-syntax-entry ?* ". 23" camel-mode-syntax-table)
  (modify-syntax-entry ?\n "> b" camel-mode-syntax-table)
  (modify-syntax-entry ?' "\"" camel-mode-syntax-table))
(add-to-list 'auto-mode-alist '("\\.cml\\'" . camel-mode))
```

使用 **eglot**（Emacs 29 及以后内置）：

```elisp
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs
               '(camel-mode . ("camel-ls" "--stdio"))))
(add-hook 'camel-mode-hook #'eglot-ensure)
```

使用 **lsp-mode**：

```elisp
(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(camel-mode . "camel"))
  (lsp-register-client
   (make-lsp-client
    :new-connection (lsp-stdio-connection '("camel-ls" "--stdio"))
    :activation-fn (lsp-activate-on "camel")
    :server-id 'camel-ls)))
(add-hook 'camel-mode-hook #'lsp-deferred)
```

### 4.3 Helix

写在 `~/.config/helix/languages.toml`（或项目内的 `.helix/languages.toml`）中：

```toml
[language-server.camel-ls]
command = "camel-ls"
args = ["--stdio"]

[[language]]
name = "camel"
scope = "source.camel"
file-types = ["cml"]
roots = [".git"]
comment-token = "//"
block-comment-tokens = { start = "/*", end = "*/" }
indent = { tab-width = 4, unit = "    " }
language-servers = ["camel-ls"]
```

Helix 用 tree-sitter 语法做高亮，而 Camel 还没有 tree-sitter 语法：文件打开后没有语法着色，但语言服务器的功能可用。

---

## 5. 修改语法文件

TextMate 语法（`editors/vscode/syntaxes/camel.tmLanguage.json`）与词法分析器自己的记号表（`include/camel/parse/syntax/token_kinds.def`）保持一致。两类测试保证这一点，它们都随默认测试集（`npm run test`）运行：

```bash
node scripts/test.js test/plans/feat/tools/grammar.plan.toml
```

- `grammar.tables` 与 `grammar.files` 不需要安装任何东西。当词法文法增删了关键字、语法文法增删了字面记号、或编译器增删了内建类型名而语法文件没有跟上时，前者会失败。
- `grammar.scopes.*` 用 VS Code 自己使用的库对一组程序分词，并把每个记号的作用域与 `test/plans/feat/tools/golden/grammar/` 下的基准文件比较。在 `editors/vscode` 中执行 `npm install` 之前，这些测试会被**跳过**。

有意修改语法文件之后，先查看新的作用域，再记录它们：

```bash
node editors/vscode/scripts/tokenize.mjs test/cases/tools/grammar/functions.cml
node test/tools/run-tests.mjs --update-golden test/plans/feat/tools/grammar.plan.toml
```
