# Frontend 分离任务入口

当前阶段：**Tasks 清单已生成待审，尚未开始实现或部署。** 本轮按用户“推进到 todo”的要求整理计划与任务，不代表特权环境准备、发布或服务器变更已经获准或执行。

## 文件入口

| 文件 | 内容与状态 |
|---|---|
| [CAPABILITY_MAP.md](../CAPABILITY_MAP.md) | 模块职责与先本机、后 Web 多机集成的顺序 |
| [SPEC-frontend-runtime.md](../SPEC-frontend-runtime.md) | 当前模块的范围和 F01–F08 验收基线 |
| [plan.md](plan.md) | 技术方案、依赖、端口、override、ARM64/Harbor 流程、验证入口与风险 |
| [todo.md](todo.md) | 22 项任务 F00–F21：F01 已完成，其余待执行；另有 G01/G02 |

当前清单细化 `frontend-runtime`。Web 内嵌与 `.64` 多机部署在本机案例验收后进入自身规格/计划，不提前执行。

## 最新边界

- 功能源码、测试和部署配置仅允许修改 `workspace/infra/web/`、`workspace/infra/frontend/`。
- **KT2/KT3 原 YAML、Dockerfile、算法、源码、测试、数据全部不改**。在 Frontend 自己目录内提供外置 Compose override，与原配置叠加运行。
- Sidecar、SDK、协议、公共发布脚本不改；复用现有观测端口配置。Frontend 发布入口放 `frontend/docker/`，沿用既有发布校验规则。
- 能力图、规格、本任务目录是文档例外。之前的仓库整理修改不属于本任务，不回滚，也不能用它们掩盖新越界修改。

## 交付要求

1. 本机分别跑通 KT2 和 KT3：TCP、无 Web，独立 Frontend 展示真实输入/输出和 RD 结果，保留原结果校验。
2. 全程使用 ARM64 Docker；x86 本机使用模拟运行，不建立 AMD64 替代应用镜像。
3. 部署优先 Harbor 拉取固定 digest，`up --no-build`；发布构建与部署验收分开，不临时现场构建替代复验。
4. 后续把同一 Frontend 嵌入 `192.162.2.64` 上的 Web，由它部署受管服务器的 Docker 链路并验收多机结果。
5. 不新增录制，不新增账号系统；Frontend 不接收 SSH 密码/私钥，停止预览不影响算法链或 Web 部署管理。
6. 页面默认 Frontend 8081、Web 8080；同机多 Frontend 递增分配页面端口，遥测/预览接收端口另配，详见计划。

## 已有证据与当前阻塞

- 已核对四份原 Compose、现有 Web/Sidecar 发布文档和本地 ARM64 镜像缓存。
- GFKD ARM64 编译产物与四份 CSV 已在本机；Git 忽略不等于缺失。
- 已依用户授权注册 qemu-aarch64，ARM64 容器实际执行通过，F01 完成；见 [环境证据](evidence/arm64-environment.md)。注册未设开机持久化，宿主重启后重新检查。
- 未实现 Frontend，未运行两个案例验收，未发布新镜像，未实施 `.64` 更新。

开始执行前确认清单；环境特权操作、发布版本/推送、影响已有容器的操作仍分别确认。任务必须附真实验证结果后才能勾选，不自动提交或推送。
