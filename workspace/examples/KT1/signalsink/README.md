# SignalSink

末端 Worker，默认只消费，显式起录后保存原始 Envelope + Metadata/Payload。Web 抽屉提供独立录制区域；无 Web 时通过本地 CLI 控制。不新增控制网页、网络端口或常驻容器。

> [!IMPORTANT]
> 当前完成源码与本机功能验证，**尚无本次 SignalSink 发布 digest，未完成原生 ARM/实际目标盘 491.52 MB/s 验收**。不要把下列构建命令或合成数据测试当作交付完成。状态见 [Todo](tasks/todo.md) 和 [验证记录](tests/acceptance.md)。

## 本地构建与测试

仓库根目录，C++20 / CMake / Python 3 环境；先构建并安装本次 SDK 到隔离目录，不覆盖系统安装：

```bash
cmake -S workspace/infra/sdk -B /tmp/signalsink-sdk-build -DBUILD_TESTING=ON
cmake --build /tmp/signalsink-sdk-build --parallel
ctest --test-dir /tmp/signalsink-sdk-build --output-on-failure
cmake --install /tmp/signalsink-sdk-build --prefix /tmp/signalsink-sdk-install
cmake -S workspace/examples/KT1/signalsink -B /tmp/signalsink-build \
  -DCMAKE_PREFIX_PATH=/tmp/signalsink-sdk-install -DBUILD_TESTING=ON
cmake --build /tmp/signalsink-build --parallel
ctest --test-dir /tmp/signalsink-build --output-on-failure
```

本地集成测试用临时 Ring 和受控生产者校验原始字节/顺序、默认不保存、起停和空输入退出；它不是 Sidecar/RDMA 或硬件采样测试。测试用临时目录并清理自己的文件，不触碰用户录制。

## 运行与控制

Worker 必须与创建输入 Ring 的 Sidecar 共享 IPC。`SIGNALSINK_INPUT` 必须是实际输入端口的具体 `type_id:type_version`，不是 `any`。保存根目录必须预先存在且可写。

容器内示例：

```bash
/app/signalsink --input 1:3 --capture-root /captures \
  --queue-bytes 536870912 --min-free-bytes 1073741824
# 另一个终端，仍在同一 Worker 容器：
/app/signalsink control status
/app/signalsink control start --directory run-a
/app/signalsink control stop --recording-id RECORDING_ID
```

`RECORDING_ID` 来自状态响应。control 子命令不会再次打开 Ring。CLI 返回单个 JSON；`ok:false` 表示操作被拒绝，不应只看进程退出码。停止先返回 stopping，查询到 idle 才表示同步收尾成功。

| 配置 | 默认 | 约束 |
|---|---|---|
| `SIGNALSINK_INPUT` / `--input` | 无 | 必须具体绑定输入端口；版本为 uint32 |
| `SIGNALSINK_CAPTURE_ROOT` / `--capture-root` | `/captures` | 已存在的目录；只由部署者配置 |
| `SIGNALSINK_QUEUE_BYTES` / `--queue-bytes` | 512 MiB | 1 KiB–8 GiB；单个帧连同 8 字节长度必须放得下 |
| `SIGNALSINK_MIN_FREE_BYTES` / `--min-free-bytes` | 1 GiB | 不小于队列容量；在真实目标卷上检查 |

目录只允许 ASCII 字母、数字、`_`、`-`、`.` 及分隔符 `/`；不接受绝对路径、空分段、隐藏分段、`..` 或符号链接跳转。最后选择的目录保存在根目录 `.signalsink-directory`，但重启不会自动续录。

控制 socket 位于容器私有 `/tmp/uestcradar-signalsink/`，同一容器只运行一个接收进程。控制连接断开不会停止录制。队列满、空间保留线触发、写入/同步失败会停止该次录制并继续消费；输入 Ring 关闭/损坏则报告输入故障并退出。SIGTERM/INT 结束接收后尝试收尾；磁盘 IO 卡死时不能保证有界完成，外部强制终止会留下未完成文件。

## 文件及校验

- 会话名为随机 128-bit ID，排他创建 `<id>.partial`，不覆写旧会话。
- 文件头 8 字节 `USINK001`。
- 每帧为小端 uint64 字节长度，紧跟未经修改的完整原始帧；不存 Slot 空闲填充。
- 正常尾部为小端 uint64 三元组：`0, written_frames, written_raw_bytes`。
- 正常停止执行文件同步并发布 `<id>.sink`、同步目录后才报告 idle；掉电可能保留同 inode 的额外 `.partial` 链接。失败不自动续录，也不删除失败文件。

```bash
python3 workspace/examples/KT1/signalsink/tests/check_capture.py /captures/run-a/RECORDING_ID.sink
python3 workspace/examples/KT1/signalsink/tests/check_capture.py /captures/run-a/RECORDING_ID.partial --allow-partial
```

校验器使用有界读取，输出完整帧数、原始字节数及原始帧序列 SHA-256。`complete` 仅证明文件格式完整，不证明物理采样连续或设备抗断电能力。没有可信上游连续性契约时，状态始终为 `sample_continuity=unverified`。

## 镜像和部署

必须先原生 ARM 构建/测试并发布含新接口的 SDK。随后按既有发布流程构建 Worker，不能使用旧 SDK digest 冒充已包含裸帧接口。

```bash
# 在原生 ARM 环境，ALGO_BASE 为已验证的新 SDK 不可变镜像引用。
docker build --build-arg ALGO_BASE="$ALGO_BASE" \
  -t signalsink:validation workspace/examples/KT1/signalsink
```

`compose.yaml` 只启动独立接收端 Worker + Sidecar + Frontend，不自带 Source；需另接一个同类型 TCP Source 到 `127.0.0.1:36337`。输入预览位于 `http://127.0.0.1:8091/`，仍无图形化录制控制。设置真实镜像、类型和保存根目录后校验配置：

```bash
# SIGNALSINK_IMAGE=实际已发布的固定 digest，不能填一个不存在的示例 hash。
# SIGNALSINK_TYPE_ID / SIGNALSINK_TYPE_VERSION 与上游一致。
# SIGNALSINK_HOST_CAPTURE_ROOT 为已批准、现存且可写的绝对路径。
docker-compose -p signalsink-local -f workspace/examples/KT1/signalsink/compose.yaml config
docker-compose -p signalsink-local -f workspace/examples/KT1/signalsink/compose.yaml up -d --no-build
docker-compose -p signalsink-local -f workspace/examples/KT1/signalsink/compose.yaml exec -T worker-node \
  /app/signalsink control status
```

Web 管理模式仍采用 strict-RDMA。仅 SignalSink 获得宿主 `/root/workspace/captures` 到 `/captures` 的挂载，录制子目录通过 Web 设置。部署前需要确认该目录的实际磁盘、权限与空间，不能把容器系统层或错误挂载当作目标存储。固定 SSH 命令只调用三种控制操作，不传录制数据，不修改 Frontend 代理。

镜像专用标识为 `component=signalsink, roles=sink, input=any, output=none`（均带 `io.uestcradar.` 前缀）；默认 Entrypoint 为 `/app/signalsink`。Web 和发布检查器只允许这个专例，部署前绑定上游具体类型。其他 Worker 保持精确类型匹配。
