# Phase 3: 单测前置熔断防御机制 (Test-First Shield)

为了防范带病代码上线，必须在本地 X86 自检容器（`uestcradar-build:local`）中首先强制执行算子自带的单元测试：

---

## 📌 1. 本地单测执行
在本地编译构建完成后，调用各个算子的本地测试二进制程序：
```bash
./build/bin/cycore_fft_plugin
```

---

## 🛑 2. 前置熔断机制
* **断言失败 (Exit Code != 0)**：
  **必须立即停止部署！** 部署系统中断流程，保留**本地开发机**上的编译目录以供排查，严禁在远端执行任何分发与覆盖！
* **断言成功 (Exit Code == 0)**：
  单测 100% 绿色通过，准许进入下一阶段。

---

## 🧪 自动化单测自检脚本

在项目专属运维技能包中，我们预置了本地单测自检与熔断控制脚本。您可以在项目根目录下通过相对路径直接调用：
* **脚本路径**：[.agents/skills/cpp_algorithm_ops/scripts/test.sh](../scripts/test.sh)
* **调用指令**：
  ```bash
  bash .agents/skills/cpp_algorithm_ops/scripts/test.sh
  ```
* **自动化行为**：自动检查本地 X86 自检镜像 `uestcradar-build:local`（无则自建），并挂载本地工作区在容器内运行 CMake 编译与单测自检。若单测报错，利用退出码在本地原地实施前置熔断。
