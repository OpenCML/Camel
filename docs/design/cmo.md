## 设计思路

要能构造出图，图节点的顺序遵循拓扑排序（保证从字节码中构建的图不会出现环），要有结构化数据构造指令，要能支持模块导入导出（符号表的设计），要有版本号（避免出现虚拟机不兼容的情况），要有常量池

cmo会存储类型，所以要考虑类型的构造过程

参考：[java class文件详解 - 一寸HUI - 博客园 (cnblogs.com)](https://www.cnblogs.com/zsql/p/12907120.html)

## 文件结构

|      类型      |        名称         |         数量          |
| :------------: | :-----------------: | :-------------------: |
|       u4       |        magic        |           1           |
|       u2       |    minor_version    |           1           |
|       u2       |    major_version    |           1           |
|       u2       | constant_pool_count |           1           |
|    cp_info     |    constant_pool    | constant_pool_count-1 |
|       u2       |    access_flags     |           1           |
|       u2       |     this_class      |           1           |
|       u2       |     super_class     |           1           |
|       u2       |  interfaces_count   |           1           |
|       u2       |     interfaces      |   interfaces_count    |
|       u2       |    fields_count     |           1           |
|   field_info   |       fields        |     fields_count      |
|       u2       |    methods_count    |           1           |
|  method_info   |       methods       |     methods_count     |
|       u2       |  attributes_count   |           1           |
| attribute_info |     attributes      |   attributes_count    |