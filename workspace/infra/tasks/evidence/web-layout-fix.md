# Web 布局修复

- 移除主链运输方式选择器；Web UI 请求固定 strict-rdma。单机 Compose 和底层显式 TCP 诊断能力未改。
- 根因：workspace-column 只有两行网格，额外插入的选择器成为第三个子项，挤占主布局并形成大片留白。
- 抽屉原本 overflow-y:auto，iframe 又固定 760px，产生嵌套滚动。改为固定视口高度的 flex 抽屉、可伸缩 iframe，默认仅 iframe 滚动。
- 取消 iframe 外重复的 transport/peer/goodput/Ring 摘要。仅 Frontend 未展示的读写位置保留在默认折叠的高级计数中，不删除遥测能力。
- 保留实际满载 Warning，不以改 UI 隐藏数据面背压。

原生 ARM 构建、Go/vet、22 项 UI 测试通过。Chrome 对真实 React SSR/CSS 和长内容 iframe 夹具检查 1440x900、1280x700、2000x1100：抽屉 scrollHeight=clientHeight；iframe 高度分别 700/500/900，内部可滚动，workspace 恢复两个子项。该检查是布局测试，不是算法验收。

更新至 `.64` Web，服务返回新资源 `/assets/index-wSqVjVe2.js`。仅重启 Web；`.32/.64/.80` 共九个 Worker/Sidecar/Frontend 容器 ID 与 StartedAt 全部保持一致，Web 已恢复三个节点遥测。旧 Web 容器保留为 `uestcradar-web-before-layout-20261007`，状态停止。Web 内存会话重置，需要重新登录；算法流未停止。

检查原始记录：`/tmp/web-layout-fix/{tests.txt,layout-check.json,main-before.json,main-after.json,deploy.txt}`。当前为用户要求的运行更新，非新的 Harbor 正式发布。
