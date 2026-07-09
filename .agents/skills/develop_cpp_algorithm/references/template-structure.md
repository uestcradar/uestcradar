# 1. 模板目录结构

基于 `develop_new_algorithm` 技能开发新算子时，优先基于 [algorithm_template](../algorithm_template) 脚手架进行开发。

```text
algorithm_template/
├── CMakeLists.txt
├── include/data.h              # 样本类型与固定矩阵形状契约
├── src/algorithm_block.cpp     # 算法 work 实现与导出宏
└── sdk/include/                # 只读 Cycore SDK 头文件
```

### 开发者修改范围：
* `include/data.h`
* `src/algorithm_block.cpp`
* 必要时修改 `CMakeLists.txt` 链接其他算法依赖库。

> [!IMPORTANT]
> 严禁手动修改模板里的 `sdk/include` 目录内容。
