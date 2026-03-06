# Camel CLI User Guide

[中文简体](cli.cn.md) | English

    camel [options] [<target file>]
    camel <command> [command-options] <target file>

For the default run command, the input file may be given as a positional argument or via `--input <file>`. At least one is required.

## Global Options

- `-v`, `--verbose`: Enable verbose output  
- `-l`, `--log-level <level>`: Set log level. Options: `debug`, `info` (default), `warn`, `error`, `off`

---

## Main Execution (Default Behavior)

Executes the specified `.cml` file or directory as the entry point. **Input file** must be provided either as a positional argument or via `--input <file>` (at least one required).

- `--input <file>`: Input file path (alternative to positional; use with `--passes` to avoid ambiguity)  
- `--passes <list>`: Comma-separated pass list (e.g. `--passes std::gir,other`). If omitted, first positional is file, rest are passes.  
- `-P`, `--profile`: Record time spent in each stage and output a performance report  
- `-S`, `--scheduler <type>`: Specify the scheduler type  
- `-t`, `--threads <num>`: Maximum number of threads  
- `-n`, `--no-cache`: Disable cache modules  
- `-r`, `--repeat <times>`: Repeat execution a number of times  
- `-I`, `--include <dir>`: Add module search path (can be used multiple times)  
- `-L`, `--stdlib <path>`: Specify standard library path (default: `./stdlib` under current directory)  
- `-E`, `--error-format <text|json>`: Error output format (default: `text`)

---

## Information Display

- `-V`, `--version`: Show version information  
- `-h`, `--help`: Show help information  
- `-d`, `--docs`: Show documentation of package or module  
- `-a`, `--about`: Show copyright and project information  
- `-z`, `--zen`: Show the Zen of Camel

---

## check: Code Health Checker

    camel check [options] <cml file or directory>

- `-i`, `--lexical-only`: Perform lexical analysis only  
- `-s`, `--syntax-only`: Perform syntax check only (excluding formatting or semantics)  
- `-O`, `--output-format <text|json>`: Output format (default: `text`)  
- `-N`, `--max-warning <num>`: Maximum number of allowed warnings  
- `-c`, `--config <file>`: Path to rule definition file  
- `-e`, `--ignore`: Ignore definition files  
- `-o`, `--output <file>`: Output file (default: console)  
- Available global options: `--verbose`, `--log-level`

---

## inspect: Intermediate State Viewer

    camel inspect [options] <cml file>

- `-t`, `-T`, `--tok`, `--token-stream`: Print lexical tokens  
- `-s`, `-S`, `--cst`, `--concrete-syntax-tree`: Print Concrete Syntax Tree (CST)  
- `-a`, `-A`, `--ast`, `--abstract-syntax-tree`: Print Abstract Syntax Tree (AST)  
- `-c`, `-C`, `--gct`, `--graph-construct-tree`: Print Graph Construction Tree (GCT)  
- `-g`, `-G`, `--gir`, `--graph-ir`: Print Graph Intermediate Representation (GIR)  
- `--tns`, `--topo-node-seq`: Print topologically sorted node sequence  
- `--gen`, `--gene-code`: Generate code from AST  
- `-p`, `-P`, `--pass-until <n>`: Execute up to a specified graph optimization stage  
- Available global options: `--verbose`, `--log-level`

---

## Related Tools

Camel CLI (`camel`) handles run, check, and inspect. Formatting is provided by a separate tool:

- **camel-format**: Format `.cml` source files. Usage: `camel-format [options] <file>`  
  - `-i`, `--inplace`: Write back to file  
  - `-t`, `--tab-size <N>`: Indent size (default 4)  
  - `-u`, `--use-tabs`: Use tabs  
  - `-q`, `--quote-prefer <single|double>`  
  - `-m`, `--max-width <N>` (default 80)

Other tools: `camel-codegen`, `camel-profiler`. All are built under `build/tools/`.

---

## Examples

```bash
# Run the main file (positional)
camel main.cml

# Run with input and passes via options (no positional required)
camel --input main.cml --passes std::gir,other

# Run and record performance
camel -P main.cml

# Format code (use camel-format, not camel format)
camel-format -i src/xxx.cml

# Syntax check only and output in JSON
camel check -s -O json src/

# Print intermediate representation
camel inspect --gir main.cml

# View Zen of Camel
camel --zen
```