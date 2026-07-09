---
name: cpp_algorithm_ops
description: C++ 雷达算法插件物理机交叉编译、代码同步、容器热部署与分布式联调的标准化运维技能。
---

# 📖 C++ 雷达算法统一部署运维手册 (cpp_algorithm_ops)

本手册是 [uestcradar](file:///home/zikun/code/common/uestcradar) 算法解耦仓库进行物理多节点部署和调试的唯一权威规范。

---

## 🗺️ 总体全分支运维流

```mermaid
graph TD
    Start([开始部署流程]) --> S1[📋 Phase 1: 解析环境变量]
    S1 --> S2_Cond{判断 ARCH 硬件架构}
    
    %% ARCH=x86 本地同构分支
    S2_Cond -->|ARCH = x86| S2_x86{本地是否有 cycore-build:latest 镜像?}
    S2_x86 -->|是: 分支 A 🚀| S2_x86_run[挂载 cycore-build-local 容器执行编译]
    S2_x86 -->|否: 分支 B 🐳| S2_x86_build[使用 Dockerfile.build 构建本地编译镜像]
    S2_x86_build --> S2_x86_run
    
    %% ARCH=arm 交叉编译分支
    S2_Cond -->|ARCH = arm| S2_arm_build[使用 Dockerfile.build_cross 构建交叉底座]
    S2_arm_build --> S2_arm_run[使用 cycore-build-cross 交叉编译出 ARM64 .so]
    
    %% 测试阶段
    S2_x86_run --> S3[🛑 Phase 3: 容器内运行算子单测]
    S2_arm_run --> S3
    
    S3 --> S3_Cond{单测是否全部通过?}
    S3_Cond -->|🔴 报错失败| S3_Fatal[🚨 触发前置熔断: 终止部署并报告人类]
    S3_Cond -->|🟢 绿灯通过| S4_Cond{判断 USE_DOCKER}
    
    %% 部署分发阶段
    S4_Cond -->|USE_DOCKER = false| S4_Host[🚀 Phase 4: 物理分发插件至宿主机挂载目录]
    S4_Cond -->|USE_DOCKER = true| S4_Docker[🚀 Phase 4: 物理分发插件并重启运行容器]
    
    S4_Host --> End([部署热重载完成])
    S4_Docker --> End
```

---

## ⚡ 核心工作流引导

本技能通过**渐进式透露原则**，按阶段将操作拆分为以下独立文档：

* 📋 **[第一阶段：环境变量配置](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/references/phase1-environment.md)**：定义远程节点拓扑、连接参数与硬件架构特性。
* 🔨 **[第二阶段：Docker隔离编译](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/references/phase2-compilation.md)**：基于架构自适应选择容器安全构建插件。
* 🛑 **[第三阶段：单测前置熔断](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/references/phase3-testing-shield.md)**：测试前置校验熔断防线，防范故障代码上线。
* 🚀 **[第四阶段：物理部署热重载](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/references/phase4-distribution.md)**：物理覆盖分发插件并重启运行容器。

## 🐳 Docker 编译镜像定义

* 📄 **[极简本地同构编译 (Dockerfile.build)](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/Dockerfile.build)**：本地 X86 原生开发隔离编译底座。
* 📄 **[极简 AArch64 交叉编译 (Dockerfile.build_cross)](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/Dockerfile.build_cross)**：跨平台 AArch64 快速交叉编译环境。

## ⚡ 自动化控制脚本

* 🛠️ **[一键自动化部署控制脚本](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/scripts/deploy_to.sh)**：直接运行该脚本，自动执行上述四阶段流水线动作。
