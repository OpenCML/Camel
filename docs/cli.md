# Camel CLI

[cml file]：以该文件或目录为入口执行

- --help：输出命令行使用手册
- --doc：输出包帮助文档，后跟指定包名
- --version：打印版本信息
- --about：打印版权等信息
- --profile：执行过程中记录各步花费时间并生成报告文件
- --scheduler：选择图调度器（后端）
- --threads：最大线程数
- --no-cache：不使用缓存的模块
- --repeat：重复执行若干次
- --include：指定当前脚本模块查找的根目录，可多选，按顺序查找，默认为target所在目录
- --stdlib：指定标准库路径（默认路径在当前目录下/stdlib文件夹）

format：自动格式化，后跟目录或cml源文件

- --tab-size：缩进空格数
- --use-tabs：使用制表符代替空格缩进
- --quote-prefer[single/double]：默认使用单引号
- --max-width：一行最大字符数控制
- --config：指定配置文件路径
- --ignore：忽略定义文件
- --inplace/-i：直接修改源文件，而不是打印结果

check：代码健康度检查，给出限定数量的警告和所有已知错误

- --lexical-only/-l：仅执行到词法分析步骤（不向后继续执行）
- --syntax-only/-s：仅执行到语法检查步骤（不向后进行格式、语义检查）
- --output-format/-O：json，输出格式
- --max-warnings/-N：允许的最大警告数
- --config/-c：规则定义文件路径
- --ignore/-e：忽略定义文件路径
- --output/-o：指定输出文件，不设置默认是控制台

inspect：打印输出中间状态

- --tokens/-tT：打印tokens
- --syntax-tree/--cst/-sS：打印（具象）语法树
- --abstract-tree/--ast/-aA：打印抽象语法树
- --graph-tree/--gct/-cC：打印图构造树
- --graph/--gir/-gG：打印图中间表示
- --pass-until/-pP：控制执行到哪一个图优化遍（默认为-1）

build

- --optimize/-o/-O：开启优化
- --rollup/-r/-R：是否打包成一个可执行模块
- --verbose/-g：输出各类信息
- --warning/-W：on/off
- --output：指定输出目录或文件名，如果指定了rollup则是文件名
- --include：指定当前脚本模块查找的根目录，可多选，按顺序查找，默认为target所在目录
- --stdlib：指定标准库路径（默认路径在当前目录下/stdlib文件夹）

serve：开启Language Server(LSP协议)

- --host
- --port

debug：步进调试执行

- --variable
- --print
- --include：指定当前脚本模块查找的根目录，可多选，按顺序查找，默认为target所在目录
- --stdlib：指定标准库路径（默认路径在当前目录下/stdlib文件夹）

camel pkg：包管理器

- list
- init
- install
- remove
- update
