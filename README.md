# Camel
An implementation of an interpreter framework based on OpenCML.

# Enjoy!
Camel riders!

## 环境搭建

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

```Clang (MSVC-like)
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
