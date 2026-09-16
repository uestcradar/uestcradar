# Qt5 距离多普勒（RDMap）算法开发基座

本目录提供固定输入输出接口、独立算法进程的适配设计和端到端验证环境。 `docker/docker-compose-infra.yaml` 启动包含真实 CPI0～CPI9 数据的黑盒数据流， `docker/docker-compose-worker.yaml` 构建和运行开发者的 Qt5 Worker。

## 目标目录结构

```text
qt5-algorithm/
├── README.md                     # 架构、接口约定和开发部署指南
├── CMakeLists.txt                # 编译 Worker、Sink 和测试，链接 Qt5 与 SDK
├── docker/                      # Docker 镜像构建与部署配置
│   ├── Dockerfile               # 基于 algo-base:GFKD，单阶段编译、测试并运行 Worker
│   ├── Dockerfile.builder       # 维护 GFKD 开发镜像：安装 Qt5 开发依赖并验证示例
│   ├── Dockerfile.infra         # 编译、测试并打包 RD Sink 基础设施镜像
│   ├── docker-compose-infra.yaml  # 数据源、脉压、Sidecar 和 RD Sink 黑盒链路
│   └── docker-compose-worker.yaml # Worker 构建运行、IPC、共享内存和输出挂载
├── src/                         # 只保留 main.cpp，按顺序组织处理流程
├── support/                     # frame_converter.h、SDK 收发与组帧、共享内存、进程管理
├── algorithm/                   # 本地准备的 ARM64 可执行文件及 CSV，不纳入 Git
├── tests/                       # Worker 和帧转换的单元测试
├── infra/                       # 测试辅助代码：数据源、结果接收与校验
└── output/                      # 运行时生成，挂载到容器 /app/output
    └── rdmap_result.pgm         # 最新一帧 RDMap 灰度图
```

## 目标架构

```text
SignalSource → PulseCompression → Qt5 Worker → RD Sink
                                    │    ↑
                       SDK 收帧、组 CPI    SDK 输出封装
                                    ↓    │
                          输入格式转换    输出格式转换
                                    ↓    ↑
                          输入共享内存    MatrixBuffer
                                    ↓    ↑
                          独立算法进程：解析 → 计算
```
Worker 与算法进程运行在同一个容器中。`src/main.cpp` 只组织处理流程，`support/frame_converter.h` 只负责格式转换。SDK 收发与 CPI 组帧、共享内存通信、`QProcess` 进程管理封装在 `support/` 支撑库中。算法进程保留自己的 CPI 解析器、计算逻辑和结果输出格式。

我们提供配套的共享内存写入与读取实现：Worker 写入，算法进程通过 `read_from_radar_date()` 读取。对方新增 `read_from_worker_shm()`，在原读取函数中按环境变量选择调用，保留原文件初始化和 CMake，重新编译。输出复用对方的 `MatrixBuffer` 和 `MatrixReader`。

输入输出协议兼容时，更新算法可执行文件及配套依赖、CSV 后重新打包发布镜像，无需修改 Worker 适配代码。

### Worker 代码职责

| 文件或目录 | 职责 |
| --- | --- |
| `src/main.cpp` | 初始化、启动算法进程，依次调用接收、转换、写入、等待结果和发送接口 |
| `support/frame_converter.h` | 完整 CPI 转算法输入字节帧；算法结果转 RD 元数据和矩阵；校验尺寸、布局及数值转换 |
| `support/` | SDK 输入输出、CPI 缓存与顺序校验、共享内存同步、结果等待、输入关联、进程启停与错误处理、图像保存 |
| `tests/` | Worker 和帧转换的单元测试，不放在 `src` 中 |
| `infra/` | 独立测试基座的数据源、Sink 和基座测试，由维护者管理 |

`main.cpp` 按业务顺序书写，主流程示意如下。以下封装接口由支撑库提供：
```cpp
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    WorkerSupport worker;
    worker.start_algorithm();
    while (worker.running()) {
        auto cpi = worker.receive_cpi();
        auto input = frame_converter::to_algorithm_frame(cpi);
        worker.write_algorithm_input(input);
        auto result = worker.wait_algorithm_result();
        auto output = frame_converter::to_rd_frame(result, cpi);
        worker.publish_rd(output, cpi);
    }
    worker.stop_algorithm();
}
```
`to_rd_frame()` 返回待发送的元数据和矩阵，支撑库通过 SDK 创建并提交实际帧。`frame_converter.h` 不启动进程、不读写共享内存；支撑库不解释算法输入字节帧，由转换层处理协议细节。支撑库内部复用 `MatrixReader` 读取输出，封装结果尺寸和矩阵后交给转换层。异常退出时也由支撑库回收算法进程。

### 共享内存接口

输入接口为 `InputChannel::write()`；读写两端使用我们提供的同一套共享内存实现，初始化时约定相同的名称、容量和同步方式。算法输入共享内存独立于平台 SDK 的上下行通道及输出 `MatrixBuffer`。
```cpp
// Worker 端：完整写入 CPI；空间不足返回 false，不覆盖未读数据。
bool InputChannel::write(const QByteArray& frame);

// 对方新增：连接共享内存并分块读取，暂无数据返回 0。
static int read_from_worker_shm(unsigned char* buffer, int maxSize);

// 对方原入口：设置 GFKD_INPUT_SHM_KEY 时调用新函数，否则保持文件模式。
int read_from_radar_date(unsigned char* buffer, int maxSize);
```
读取允许分块，必须保持字节顺序，不重复、不丢弃未读数据；对方现有解析器负责拼出完整 CPI。启动算法进程前，Worker 初始化输入通道并建立输出共享内存连接；算法进程保留文件加载初始化，实际读取优先使用共享内存；共享内存暂无数据时不回退到文件。Worker 停止时回收子进程；子进程退出或结果超时时，本次请求失败，不将后续结果配给旧输入。

结果通过对方现有接口读取，相关源码封装进支撑库：
```cpp
bool MatrixReader::getLatestMatrix(int& m, int& n, std::vector<double>& A);
// m：频点数；n：距离单元数；A[freq * n + range]：结果值。
```
该函数每次消费一条结果记录。现有输出协议不带 CPI 编号，因此本设计每次只提交一个 CPI，读取结果后再提交下一个，并保留对应 SDK 输入帧的关联信息。若需多个 CPI 并发，须先给输入输出协议增加关联编号。

### 输入约定

平台帧由 **Metadata 结构体 + 数据矩阵** 组成。`PulseCompressionFrame` 和 `RDFrame` 是 SDK 封装类，通过 `.metadata()` 和 `.data()` 访问内容。下面展示公开字段，开发代码直接包含 `<data.h>`，不要重复定义或将结构体内存直接作为通信帧发送。

输入使用 `Input<PulseCompressionFrame>`（`2:2`）。单脉冲接口定义为：
```cpp
struct PulseCompressionMetadata {
    uint32_t channel_count;      // 通道数，例如 1
    uint32_t range_bin_count;    // 每个脉冲的距离单元数，例如 22196
    uint32_t pulse_index;        // CPI 内脉冲序号，0～63，供 Worker 组帧
    uint32_t pulses_per_cpi;     // 每个 CPI 的脉冲数，例如 64
    double range_resolution_m;  // 距离单元间隔（米），供坐标换算
};
struct ComplexFloat32 {
    float i;                    // 实部
    float q;                    // 虚部
};
// 一帧的数据矩阵：ComplexFloat32[channel_count][range_bin_count]
// 通过 SDK 访问，不是自行声明变长 C++ 数组：
auto pulse = input.read();
auto metadata = pulse.metadata();
auto matrix = pulse.data();
auto sample = matrix[0][100];     // 第 0 通道、第 100 个距离单元
```
单通道下，Worker 按 `pulse_index` 连续收齐 64 帧，转换为对方解析器要求的完整 CPI 字节帧。缺帧或错序时放弃不完整 CPI。
```cpp
// 线格式示意，逐字段按小端序列化，不直接发送 C++ 结构体内存。
struct AlgorithmInputHeader {
    uint8_t magic[16];       // BC 1C AA FF FF FF AA FF FF 7F FF 7F FF 7F FF 7F
    uint16_t range_count;   // 距离单元数，例如 22196
    uint16_t pulse_count;   // 脉冲数，例如 64
};
struct AlgorithmComplex {
    float real;             // SDK sample.i
    float imag;             // SDK sample.q
};
// 数据紧跟 20 字节头部：AlgorithmComplex[pulse_count][range_count]
// 顺序：先脉冲、后距离；每个元素 I、Q 各 4 字节。
// 总长度：20 + pulse_count * range_count * 8 字节。
```
Worker 只转换帧格式，输入仍为 float32 复数；算法进程内部再转换为计算需要的 `complex_double`。写入前校验距离单元不超过对方解析器上限 60000，并校验 uint16 字段及共享内存容量。

保护单元、参考单元、滤波器和窗函数由算法进程管理，不属于平台 Metadata。对应 CSV 随算法程序交付，并放在算法进程可查找的工作目录。

### 输出约定

算法进程写入 `MatrixBuffer` 的结果记录为：
```cpp
// 线格式示意：小端，12 字节头部。
struct AlgorithmOutputHeader {
    uint32_t magic;          // 0xAA55AA55
    int32_t m;              // 频点数
    int32_t n;              // 距离单元数
};
// 后接 m*n 个 double：外层遍历距离，内层遍历频点。
// 总长度：12 + m*n*8 字节。
```
Worker 使用 `MatrixReader` 读取并还原结果矩阵，封装为 `Output<RDFrame>`（`3:2`）：
```cpp
struct RDMetadata {
    uint32_t channel_index;       // 输入通道编号，单通道为 0
    uint32_t range_bin_count;     // 算法返回 f_CellNum，例如 22196
    uint32_t doppler_bin_count;   // 算法返回 f_FreqNum，例如 64
    double range_resolution_m;   // Worker 填写的距离单元间隔（米）
    double velocity_resolution_mps; // Worker 填写的速度单元间隔（米/秒）
};
// 输出矩阵：float[range_bin_count][doppler_bin_count]
// 元素由算法结果转换：
rd.data()[cell][freq] = static_cast<float>(A[freq * n + cell]);
```
例如算法返回 64 个频点、22196 个距离单元：
```text
算法结果：double[64][22196]，按 [频点][距离] 访问
平台输出：float[22196][64]，按 [距离][频点] 访问
```
输出尺寸以算法返回值为准，不固定为 65 列，不补列、不截断。输出不需要 `pulse_index`。算法进程不必返回距离、速度分辨率，由 Worker 从已确认的配置填写；距离网格不变时，距离分辨率也可从输入透传。固定网格可使用固定配置值。结果的数值含义和坐标须符合 RD 帧定义，其他结果类型由 SDK 统一定义契约。

Worker 校验返回尺寸、数值及 double 转 float 的有效性，Sink 根据帧中实际维度验证。SDK 负责封包；RD 通信 Metadata 占 32 字节，单帧大小满足：
```text
32 + range_bin_count × doppler_bin_count × 4 <= 33,554,432 字节
```

### 算法交付与验证

- 对方交付 Linux ARM64 可执行文件、运行依赖和滤波器 CSV，在输入源文件中新增读取函数和调用，原 CMake 无需修改。
- Worker 链接 SDK、Qt5 Core 和支撑库；SDK 调用代码使用 C++20，无需链接算法 `.a`。
- 格式测试验证 CPI 组帧、输入字节布局、共享内存分块读写、输出矩阵转换及实际尺寸。
- 进程测试验证启动、退出、结果超时和停止清理；数值测试使用同一输入、参数和滤波器对比参考结果。
- 端到端测试验证上游脉压、Worker、算法进程和 Sink；格式 PASS 不代表算法数值正确。维护者同步调整示例测试及 Sink 的维度约束，按真实输出验证。

## 修改边界

开发者保持 `docker/docker-compose-infra.yaml`、Worker 的 IPC 配置及 `/uestcradar_qt5_algorithm_up`、`/uestcradar_qt5_algorithm_down` 通道名称不变。基础设施与 Sink 的契约调整由维护者统一完成。

Worker 基于 `registry.chengyistudio.com/cxx/algo-base:GFKD` 构建，镜像标签保持 `worker/v2`、`operator`、输入 `2:2`、输出 `3:2`。算法适配代码、可执行文件、依赖、资源、测试及构建文件可按工程需要调整。

## 开发与部署

运行要求：Docker Engine 20.10 或更新版本（API ≥ 1.41）及 Docker Compose v2。Docker 19.03 无法通过这里的 Compose v2 配置创建带 `platform` 参数的容器。

以下命令均在 `qt5-algorithm/` 工程根目录执行。Docker 文件集中在 `docker/`，构建上下文仍为工程根目录，输出仍保存在根目录的 `output/`。

### 第一步：启动黑盒测试基座

基础镜像是 ARM64。x86_64 主机先检查模拟支持；未注册时执行安装，再启动基座：
```bash
test -r /proc/sys/fs/binfmt_misc/qemu-aarch64 || \
  docker run --privileged --rm tonistiigi/binfmt --install arm64
```
ARM64 主机跳过上述命令。首次使用私有仓库时，先执行 `docker login registry.chengyistudio.com`。然后复制工程并启动：
```bash
cp -a /path/to/uestcradar/workspace/examples/qt5-algorithm ~/my-rd-algorithm
cd ~/my-rd-algorithm
docker build --platform linux/arm64 -f docker/Dockerfile.infra -t uestcradar/rd-sink:gfkd-local .
export RD_SINK_IMAGE=uestcradar/rd-sink:gfkd-local
docker compose -f docker/docker-compose-infra.yaml up -d
```
不要在这个命令中合并 Worker Compose。

### 第二步：接入算法进程

构建 Worker 前先准备算法程序；本例使用 GFKD ARM64 程序完成验证。

入口 `src/main.cpp` 保留 `QCoreApplication`，通过支撑库组织流程，格式转换写在 `support/frame_converter.h`：

1. 调用支撑库初始化共享内存、启动算法程序并设置 CSV 所在工作目录。
2. 支撑库接收并组完整 CPI；转换层生成对方的 CPI 字节帧，再由支撑库写入输入共享内存。
3. 支撑库等待并读取结果；转换层校验尺寸和数值，生成 `RDMetadata` 和输出矩阵。
4. 支撑库保存 RDMap，通过 SDK 创建并提交与输入关联的输出，再处理下一个 CPI；停止时清理算法子进程。

对方仅在 `ReciveManager/radar_data_source.cpp` 增加 `read_from_worker_shm()` 及必要头文件，并在 `read_from_radar_date()` 中按 `GFKD_INPUT_SHM_KEY` 调用；不修改原文件初始化、main、CMake、算法、解析器和输出。Worker 的 CMake 编译 `src/main.cpp` 并链接支撑库，单元测试源码从 `tests/` 编译；Dockerfile 纳入支撑库、测试及算法可执行文件、运行依赖和 CSV，并保证程序可执行。算法更新后沿用下面的构建、验证和发布流程。

在本地准备算法产物（`GFKD_SOURCE` 指向已接入读取函数的工程）：
```bash
export GFKD_SOURCE=/home/zikun/Documents/cxx/GFDK/GFKD_V1_ARM/GFKD_V1_ARM
mkdir -p algorithm output
docker run --rm --platform linux/arm64 --user "$(id -u):$(id -g)" \
  --entrypoint sh -v "$GFKD_SOURCE:/vendor:ro" -v "$PWD/algorithm:/artifacts" \
  registry.chengyistudio.com/cxx/algo-base:GFKD -c '
    cmake -S /vendor -B /tmp/build -DCMAKE_BUILD_TYPE=Release -DALGO_LINK_MODE=STATIC &&
    cmake --build /tmp/build --parallel 4 &&
    cp /tmp/build/GFKD_V1_ARM /artifacts/ &&
    cp /vendor/subband_filter_*.csv /artifacts/'
```
Worker 默认启动 `/app/algorithm/GFKD_V1_ARM`，工作目录为 `/app/algorithm`。运行日志保存为 `output/algorithm.log`；原程序的 `send_data.bin` 通过软链接写到 `output/algorithm_send_data.bin`。输入通道由 Worker 创建，32 MiB，头部四个 uint32 字段为 magic `0x47504631`、容量、有效长度、已读位置；双方使用 Qt 共享内存锁同步。

SDK 帧 API 见 [SDK 接口指南](../../sdk/README.md)，矩阵布局以 [脉压帧契约](../../sdk/contracts/pulse_compression.json)和 [RD 帧契约](../../sdk/contracts/rd.json)为准。

### 第三步：构建本地 RD 算法镜像

Compose 自动拉取 `algo-base:GFKD`，使用其中的 Qt5、SDK 和编译工具构建并测试 Worker，无需在 Worker Dockerfile 中重新安装 Qt5。编译和运行使用同一个阶段，镜像保留 `/src` 源码、`/build` 编译产物和开发工具，入口为 `/app/qt5-algorithm`。
```bash
docker compose -f docker/docker-compose-worker.yaml build
```
本地开发镜像为 `uestcradar/rd-algorithm:dev`。该命令不会构建或重启黑盒 Infra。

### 第四步：挂载运行并查看 PASS 日志

前台启动 Qt5 Worker：
```bash
docker compose -f docker/docker-compose-worker.yaml up
```
另一个终端查看真实数据流和 QA Sink：
```bash
docker compose -f docker/docker-compose-infra.yaml logs -f signalsource pulsecompression rd-sink
```
结构校验通过时会持续看到：
```text
[PASSED] RDFrame received=<n> shape=<range>x<doppler> peak_range=<r> peak_doppler=<d> magnitude=<v>
```
最新一帧 RDMap 同时保存在 `output/rdmap_result.pgm`。

修改代码后可单独重启 Worker，无需重启测试基座：
```bash
docker compose -f docker/docker-compose-worker.yaml up --build
```
在另一个终端停止时，先停 Worker，再停黑盒基础设施：
```bash
docker compose -f docker/docker-compose-worker.yaml down
docker compose -f docker/docker-compose-infra.yaml down
```

### 完整本地验收与 RD 图导出

先完成算法产物准备、Worker 和本地 Sink 构建。验收前停止上一轮 Worker 和 Infra；将需要保留的旧 `output/` 结果另存，再创建空的 `output/`，避免追加记录混入本轮。
```bash
docker run --rm --platform linux/arm64 --entrypoint sh \
  -v "$PWD:/project:ro" registry.chengyistudio.com/cxx/algo-base:GFKD -c '
    cmake -S /project -B /tmp/tests -DBUILD_INFRA=ON -DBUILD_TESTING=ON &&
    cmake --build /tmp/tests --parallel 4 &&
    ctest --test-dir /tmp/tests --output-on-failure'
export RD_SINK_IMAGE=uestcradar/rd-sink:gfkd-local
export SOURCE_FRAMES=10 SINK_FRAMES=10 WORKER_FRAMES=10
docker compose -f docker/docker-compose-infra.yaml up -d
docker compose -f docker/docker-compose-worker.yaml up --abort-on-container-exit --exit-code-from rd-algorithm
```
Sink 应收到 10 帧，Worker 正常退出。日志中应有对应 CSV 加载记录，不应出现丢帧、超时或 FAIL。`GFKD_TIMEOUT_MS` 默认 600000，可按运行环境设置。
```bash
docker compose -f docker/docker-compose-infra.yaml logs --no-color > output/infra.log
docker compose -f docker/docker-compose-worker.yaml logs --no-color > output/worker.log
python3 tests/export_results.py output --frames 10
```
导出脚本在主机运行，需要 NumPy、Pillow 和 Matplotlib。脚本逐帧比对算法 double 结果与 SDK float32 数据，并与 Sink 日志中的现有摘要核对，确认下游收到相同矩阵，导出最后一帧：

- `rdmap_result.pgm`：Worker 保存的原始尺寸灰度图。
- `rdmap_result.npy`：原始 float32 矩阵，按 `[距离][频点]` 存储。
- `rdmap_full_resolution.png`：完整分辨率图，横轴距离、纵轴频点。
- `rdmap_result.png`：带坐标和色标的概览图，距离轴按区间最大值缩减，保留峰值。
- `verification.json`：帧数、实际维度、数值范围和比对结果。

结果验收后，使用第四步的 `down` 命令清理。此验收证明接入与格式转换一致，不代替算法精度评估。

### 第五步：发布 RD 算法镜像

完成自己的算法验证后再发布；仅测试开发环境时，到第四步即可。

发布名称固定为：
```text
registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0
```
执行：
```bash
docker tag uestcradar/rd-algorithm:dev registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0
docker push registry.chengyistudio.com/cxx/worker:rd-algorithm-v1.0.0
```

## 维护 GFKD 开发镜像

`docker/Dockerfile.builder` 单独维护 Qt5 开发依赖；`docker/Dockerfile` 用于日常 Worker 构建。只有开发环境依赖需要更新时，维护者才在本目录执行以下命令（ARM64 主机）：
```bash
docker build --pull -f docker/Dockerfile.builder --target builder \
  -t registry.chengyistudio.com/cxx/algo-base:GFKD .
docker push registry.chengyistudio.com/cxx/algo-base:GFKD
```
开发镜像从 `algo-base:latest` 构建，安装 Qt5 开发包并编译、测试示例。算法开发者拉取 `algo-base:GFKD` 即可使用，无需维护这份构建文件。
