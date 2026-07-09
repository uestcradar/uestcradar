# Phase 3: 单测前置熔断防御机制 (Test-First Shield)

为了防范带病代码上线，在编译容器中必须首先强制执行算子自带的单元测试：

1. **测试目标执行**：
   在编译容器中，对新编译生成的单元测试二进制程序（如 `qa_fft_block`）发起执行。
   ```bash
   ./build/bin/qa_fft_block
   ```
2. 🛑 **前置熔断机制**：
   * **断言失败 (Exit Code != 0)**：
     **必须立即停止部署！** 部署程序抛出 `[FATAL] Integration Test Failed!`，完整保留远端物理编译目录与 core dump 现场以供开发者排查，**严禁将动态库拷贝入插件目录**！
   * **断言成功 (Exit Code == 0)**：
     测试通过，输出 `TEST PASSED`，流程被准许进入第四步。
