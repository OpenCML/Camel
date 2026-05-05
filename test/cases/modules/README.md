# 模块测试用例

本目录包含各 .cmo 模块的测试用例，运行方式：

```bash
# io 模块（print/println，globals 预加载）
camel test/modules/io/basic.cml

# os 模块（whoami, sleep 等）
camel test/modules/os/basic.cml

# math 模块
camel test/modules/math/basic.cml

# time 模块
camel test/modules/time/basic.cml

# random 模块
camel test/modules/random/basic.cml

# algo 模块（sort, sorted, merge_sorted_arrays）
camel test/modules/algo/basic.cml

# statistics 模块（mean, stdev）
camel test/modules/statistics/basic.cml

# this 模块（zen）
camel test/modules/this/basic.cml

# sys 模块（get_env, get_cwd）
camel test/modules/sys/basic.cml

# fs 模块（read_text, write_text）
camel test/modules/fs/basic.cml

# re 模块（test）
camel test/modules/re/basic.cml

# json 模块（encode, decode）
camel test/modules/json/basic.cml

# para 模块（导入测试）
camel test/modules/para/basic.cml

# python 模块（需 Python 环境）
camel test/modules/python/basic.cml
camel test/modules/python/main.cml

# pyplot 模块
camel test/modules/pyplot/simple.cml
camel test/modules/pyplot/main.cml
```
