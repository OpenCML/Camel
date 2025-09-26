# Camel CLI 使用说明

[English](cli.md) | 中文简体

    camel [options] <target file>
    camel <command> [command-options] <target file>

## 通用选项（Global Options）

- `-v`, `--verbose`：启用详细输出
- `-l`, `--log-level <level>`：设置日志等级，支持：`debug`, `info`（默认）, `warn`, `error`, `off`

---

## 主执行入口（默认行为）

以指定 `.cml` 文件或目录作为入口执行：

- `-P`, `--profile`：记录执行各阶段耗时，并输出分析报告
- `-S`, `--scheduler <type>`：指定图调度器类型
- `-t`, `--threads <num>`：最大线程数
- `-n`, `--no-cache`：不使用缓存模块
- `-r`, `--repeat <times>`：重复执行若干次
- `-I`, `--include <dir>`：添加模块搜索路径（可多次指定）
- `-L`, `--stdlib <path>`：指定标准库路径（默认当前目录下的 `./stdlib`）
- `-E`, `--error-format <text|json>`：错误输出格式，默认为 `text`

---

## 信息展示（Information）

- `-V`, `--version`：显示版本信息
- `-h`, `--help`：显示帮助信息
- `-d`, `--docs`：显示包或模块文档
- `-a`, `--about`：显示版权和项目信息
- `-z`, `--zen`：展示 Camel 的哲学（Zen）

---

## format：代码格式化工具

    camel format [options] <cml文件或目录>

- `-t`, `--tab-size <num>`：缩进空格数（默认为 4）
- `-u`, `--use-tabs`：使用制表符代替空格
- `-q`, `--quote-prefer <single|double>`：默认使用的字符串引号类型（默认为 `single`）
- `-m`, `--max-width <num>`：一行最大字符数（默认 100）
- `-c`, `--config <file>`：指定配置文件路径
- `--ignore`：忽略定义文件
- `-i`, `--inplace`：就地修改源文件，而不是输出到控制台
- 可用的通用选项：`--verbose`, `--log-level`

---

## check：代码健康检查

    camel check [options] <cml文件或目录>

- `-i`, `--lexical-only`：仅词法分析
- `-s`, `--syntax-only`：仅语法检查（不包括格式或语义）
- `-O`, `--output-format <text|json>`：输出格式（默认为 `text`）
- `-N`, `--max-warning <num>`：最大允许警告数
- `-c`, `--config <file>`：规则定义文件路径
- `-e`, `--ignore`：忽略定义文件
- `-o`, `--output <file>`：输出文件（默认输出到控制台）
- 可用的通用选项：`--verbose`, `--log-level`

---

## inspect：中间状态查看工具

    camel inspect [options] <cml文件>

- `-t`, `-T`, `--tok`, `--token-stream`：打印词法 tokens
- `-s`, `-S`, `--cst`, `--concrete-syntax-tree`：打印具象语法树（CST）
- `-a`, `-A`, `--ast`, `--abstract-syntax-tree`：打印抽象语法树（AST）
- `-c`, `-C`, `--gct`, `--graph-construct-tree`：打印图构造树（GCT）
- `-g`, `-G`, `--gir`, `--graph-ir`：打印图中间表示（GIR）
- `--tns`, `--topo-node-seq`：打印拓扑排序后的节点序列
- `--gen`, `--gene-code`：从 AST 生成代码
- `-p`, `-P`, `--pass-until <n>`：执行到指定图优化阶段
- 可用的通用选项：`--verbose`, `--log-level`

---

## 示例

```bash
# 执行主文件
camel main.cml

# 执行并记录性能
camel -P main.cml

# 格式化代码
camel format src/xxx.cml

# 只检查语法并输出为 JSON
camel check -s -O json src/

# 打印中间表示
camel inspect --gir main.cml

# 查看 Zen
camel --zen
```