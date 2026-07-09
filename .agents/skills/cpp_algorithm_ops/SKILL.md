---
name: cpp_algorithm_ops
description: C++ 雷达算法插件物理机交叉编译、代码同步、容器热部署与分布式联调的标准化运维技能。
---

# 📖 C++ 雷达算法统一部署运维手册 (cpp_algorithm_ops)

本手册是 [uestcradar](file:///home/zikun/code/common/uestcradar) 算法解耦仓库进行物理多节点部署和调试的唯一权威规范。我们采用 **“环境定义 -> 隔离编译 -> 测试熔断 -> 物理部署”** 四步防御性部署流，保障雷达线上打流系统的零故障与高可靠性。

---

## ⚡ 核心工作流引导

我们建议通过以下两份文档，快速了解并触发自动化部署与测试流程：

* 📖 **[四步安全部署与前置熔断工作流](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/references/workflow.md)**：了解环境变量、自适应 Dockerfile 编译、前置测试熔断和 `.so` 热加载的具体实现原理。
* 🛠️ **[一键自动化部署控制脚本](file:///home/zikun/code/common/uestcradar/.agents/skills/cpp_algorithm_ops/scripts/deploy_to.sh)**：直接运行此脚本，一键完成 Sync、Build、Test 和 Deploy。

---

## 🚀 部署指令快速参考

进入 `uestcradar` 项目根目录，运行部署控制脚本，传入对应的节点 ID（如 `node2` 或 `node4`）：

```bash
cd /home/zikun/code/common/uestcradar

# 部署并热重载 node2 节点 (ARM 物理板卡)
bash .agents/skills/cpp_algorithm_ops/scripts/deploy_to.sh node2

# 部署并热重载 node4 节点 (ARM 物理板卡)
bash .agents/skills/cpp_algorithm_ops/scripts/deploy_to.sh node4
```
