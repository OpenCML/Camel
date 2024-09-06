# Camel

An implementation of an interpreter framework based on OpenCML.

## 基础用法

camel `<target> `stdin/未指定默认进入交互模式

-dump-ast

-dump-cst

-stdlib 指定标准库路径/默认安装目录下stdlib目录

-i --include 其他cml库查找目录，默认target所在目录，可多选（用什么分隔？

-e --extern 外部依赖项/脚本接口

-o --output stdout camel自身输出

-v --version

-g/d --debug | --verbose | 调试模式

-c --check 检查语法错误和语义错误

-gen-graph

-cache 是否生成缓存/缓存路径/文件名 | -nocache

-sync 强制同步 | -async

-engine/resolve 指定graph序列化的执行引擎

-t --threads 指定最大线程数

-p --port 8080 使用正在运行的server

-f --out-format

-h --help

camel host 持久运行，监听端口，保持内存，提升性能

-p --port

-t --threads

-m --modules 支持缓存的最大模块数

-lru 内存管理策略

--syntax-only 只进行语法检查

--semantic-only 只进行语义检查


run 执行（输出执行结果）

format 格式化 --config（输出格式化后内容）

parse（输出中间表示）

build：转成中间形式cst/ast/gir/gcc

stage

- 

help（输出帮助文档）

host：开启服务监听模式（标准输入输出，执行）

test：检查是否有语法语义问题（输出代码健康度检查）

analysis：需要分析变量关系（怎么做

version：（输出版本信息）

pref：开启性能监控（运行时）（输出运行结果并保存文件）
debug

# Enjoy!

Camel riders!

# 环境搭建

安装Cmake

> cmake-3.26 及以上版本，若使用MSVC编译器，建议使用VS自带的cmake，并将其添加到环境变量

[Download | CMake](https://cmake.org/download/)

安装Conan

> Conan是python实现的C++包管理工具，安装2.0及以上版本

```
pip install conan
```

环境扫描

```
conan profile detect --force
```

参考环境（C:\Users\user\\.conan2\profiles\default）

```MSVC
[settings]
arch=x86_64
build_type=Release
compiler=msvc
compiler.cppstd=20
compiler.version=193
compiler.runtime=static
os=Windows
```

```Clang
[settings]
os=Windows
arch=x86_64
build_type=Release
compiler=clang
compiler.version=16
compiler.cppstd=20
compiler.runtime=dynamic
compiler.runtime_type=Release
compiler.runtime_version=v143

[conf]
tools.cmake.cmaketoolchain:generator=Visual Studio 17
```

查看msvc版本：[Visual Studio版本号、MSVC版本、工具集版本号_查看msvc版本_sanqima的博客-CSDN博客](https://blog.csdn.net/sanqima/article/details/117849324)

安装依赖

```
conan install . --output-folder=build --build=missing
```

运行脚本

```
./run.cmd
```

conan会自动完成依赖包的获取和编译，但在链接阶段会报错，说找不到`antlr4-runtime`
这里主要原因是conan对antlr4包的命名有误（具体原因不详，但有walkaround：

> 找到conan存储依赖包的路径
> 打开文件 `./build/conan_toolchain.cmake`
> 搜索 `CMAKE_LIBRARY_PATH`
> 找到后面的路径
> 类似于 `"C:/Users/xxxx/.conan2/p/b/antlrxxxxxxxxxxx/p/lib"`
> 打开该路径，将其中的`antlr4-runtime-static.lib`重命名为`antlr4-runtime.lib`
> 重新执行`run.cmd`即可