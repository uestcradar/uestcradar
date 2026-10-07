# Qt5 距离多普勒（RDMap）算法开发基座

本目录提供固定输入输出接口、独立算法进程的适配设计和端到端验证环境。根目录 `compose.yaml` 在一个 project 中部署真实 CPI0～CPI9 → 脉压 → GFKD → RD Sink，以及各节点独立 Frontend；不部署 Web/Nginx。

## 目标目录结构

```text
KT3/
├── README.md                     # 架构、接口约定和开发部署指南
├── compose.yaml                 # 单一部署入口，固定 ARM64 Harbor digest
├── CMakeLists.txt                # 编译 Worker、Sink 和测试，链接 Qt5 与 SDK
├── docker/                      # Docker 镜像构建与部署配置
│   ├── Dockerfile               # 基于 algo-base:GFKD，单阶段编译、测试并运行 Worker
│   ├── Dockerfile.builder       # 维护 GFKD 开发镜像：安装 Qt5 开发依赖并验证示例
│   └── Dockerfile.infra         # 编译、测试并打包 RD Sink 基础设施镜像
├── src/                         # 只保留 main.cpp，按顺序组织处理流程
├── support/                     # frame_converter.h、SDK 收发与组帧、共享内存、进程管理
├── algorithm/                   # 本地准备的 ARM64 可执行文件及 CSV，不纳入 Git
├── tests/                       # Worker 和帧转换的单元测试
└── infra/                       # 测试辅助代码：数据源、结果接收与校验
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
| `support/` | SDK 输入输出、CPI 缓存与顺序校验、共享内存同步、结果等待、输入关联、进程启停与错误处理 |
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

开发者保持 `compose.yaml` 中的 Worker IPC/SHM/健康依赖及 `/uestcradar_qt5_algorithm_up`、`/uestcradar_qt5_algorithm_down` 通道名称不变。基础设施与 Sink 的契约调整由维护者统一完成。

Worker 基于 `registry.chengyistudio.com/cxx/algo-base:GFKD` 构建，镜像标签保持 `worker/v2`、`operator`、输入 `2:2`、输出 `3:2`。算法适配代码、可执行文件、依赖、资源、测试及构建文件可按工程需要调整。

## 单机部署

运行要求：Docker Engine 20.10 或更新版本、Compose v2、Harbor 访问权限；x86_64 主机须已具备 ARM64 binfmt/QEMU。主机准备不是日常部署步骤，不自动提权安装模拟器。

从本目录执行唯一启动命令：

```bash
docker compose up -d --no-build
```

无额外 `.env`、业务变量、override 或现场构建；缺失镜像自动拉取，失败明确报错。默认 `functional / tcp,self`，Worker 共享所属 Sidecar 的 IPC，保留 SHM 与健康依赖，主链不依赖 Frontend。

| 页面 | 地址 |
|---|---|
| IQ Source | http://127.0.0.1:8081 |
| 脉压 | http://127.0.0.1:8082 |
| GFKD 输入 / RD 输出 | http://127.0.0.1:8083 |
| RD Sink | http://127.0.0.1:8084 |

```bash
docker compose ps
docker compose logs -f rd-algorithm rd-sink
docker compose down
```

先停止 KT2，再启动 KT3，两者复用端口；已有同名 project 或端口占用时先确认，不覆盖其他工作负载。QEMU 只验功能，不代表原生性能。服务健康不代表算法已输出；有效结果必须有更新的 frame_id、真实绘图与 Sink 校验。

当前 GFKD 输出为 22196 × 64。Compose 使用仓库现有动态尺寸校验器构建的 Sink，不再使用固定要求 65 列的旧 `rd-sink-latest` 镜像。未修改算法、数据、测试或 Dockerfile，也不裁剪/补列来迁就旧 Sink。

## 算法开发（维护者，不是日常部署步骤）

### 接入算法进程

构建 Worker 前先准备算法程序；本例使用 GFKD ARM64 程序完成验证。

入口 `src/main.cpp` 保留 `QCoreApplication`，通过支撑库组织流程，格式转换写在 `support/frame_converter.h`：

1. 调用支撑库初始化共享内存、启动算法程序并设置 CSV 所在工作目录。
2. 支撑库接收并组完整 CPI；转换层生成对方的 CPI 字节帧，再由支撑库写入输入共享内存。
3. 支撑库等待并读取结果；转换层校验尺寸和数值，生成 `RDMetadata` 和输出矩阵。
4. 支撑库通过 SDK 创建并提交与输入关联的输出，再处理下一个 CPI；停止时清理算法子进程。

对方仅在 `ReciveManager/radar_data_source.cpp` 增加 `read_from_worker_shm()` 及必要头文件，并在 `read_from_radar_date()` 中按 `GFKD_INPUT_SHM_KEY` 调用；另关闭 `SendDataManager` 的 `send_data.bin` 文件保存；计算和共享内存输出格式保持不变。Worker 的 CMake 编译 `src/main.cpp` 并链接支撑库，单元测试源码从 `tests/` 编译；Dockerfile 纳入支撑库、测试及算法可执行文件、运行依赖和 CSV，并保证程序可执行。算法更新后沿用下面的构建、验证和发布流程。

在原生 ARM 构建机准备算法产物（`GFKD_SOURCE` 指向已接入读取函数的工程；日常部署直接使用已发布镜像）：
```bash
export GFKD_SOURCE=/home/zikun/Documents/cxx/GFDK/GFKD_V1_ARM/GFKD_V1_ARM
mkdir -p algorithm
test "$(uname -m)" = aarch64
docker run --rm --user "$(id -u):$(id -g)" \
  --entrypoint sh -v "$GFKD_SOURCE:/vendor:ro" -v "$PWD/algorithm:/artifacts" \
  registry.chengyistudio.com/cxx/algo-base:GFKD -c '
    cmake -S /vendor -B /tmp/build -DCMAKE_BUILD_TYPE=Release -DALGO_LINK_MODE=STATIC &&
    cmake --build /tmp/build --parallel 4 &&
    cp /tmp/build/GFKD_V1_ARM /artifacts/ &&
    cp /vendor/subband_filter_*.csv /artifacts/'
```
Worker 默认启动 `/app/algorithm/GFKD_V1_ARM`，工作目录为 `/app/algorithm`。算法日志直接转发到容器控制台；不生成 `algorithm.log`、`send_data.bin`、`rd_frames.bin` 或 PGM 文件。输入通道由 Worker 创建，32 MiB，头部四个 uint32 字段为 magic `0x47504631`、容量、有效长度、已读位置；双方使用 Qt 共享内存锁同步。

SDK 帧 API 见 [SDK 接口指南](../../infra/sdk/README.md)，矩阵布局以 [脉压帧契约](../../infra/sdk/contracts/pulse_compression.json)和 [RD 帧契约](../../infra/sdk/contracts/rd.json)为准。

### 构建、验证与发布

Worker Dockerfile 复用 GFKD 基座的 Qt5/SDK，已有 CMake/CTest 测试仍是发布门槛。Worker 入口为 `/app/qt5-algorithm`，保留原有 `/src`、`/build`；Sink 使用原 `docker/Dockerfile.infra`，不修改或绕过校验。

需要独立运行源码测试时，在 ARM 构建机执行：

```bash
test "$(uname -m)" = aarch64
docker run --rm --entrypoint sh \
  -v "$PWD:/project:ro" registry.chengyistudio.com/cxx/algo-base:GFKD -c '
    cmake -S /project -B /tmp/tests -DBUILD_INFRA=ON -DBUILD_TESTING=ON &&
    cmake --build /tmp/tests --parallel 4 &&
    ctest --test-dir /tmp/tests --output-on-failure'
```

按 [既有发布流程](../../../.agents/skills/docker-release/SKILL.md) 从明确的源码版本构建、测试并发布新的不可变镜像，再更新 Compose digest；本机只从 Harbor 拉取，不用 `:dev`、save/load 或源码挂载替代部署。不要覆盖已发布的 `rd-algorithm-v1.0.0`。

真实运行应持续出现 Worker 完成计数、CSV 加载记录及以下 Sink 结果：

```text
[PASSED] RDFrame received=<n> shape=<range>x<doppler> peak_range=<r> peak_doppler=<d> magnitude=<v>
```

Frontend 的 RD 图横轴为距离、纵轴为多普勒；检查原始维度、帧号和色标，不能把格式校验当作算法精度证明。`GFKD_TIMEOUT_MS` 保留 600000 ms 默认值，可在配置中按实际算法耗时调整。运行数据仍通过共享内存传递，不新增录制功能；历史文件仍可用原 `tests/export_results.py` 离线分析。

## 维护 GFKD 开发镜像

`docker/Dockerfile.builder` 单独维护 Qt5 开发依赖；`docker/Dockerfile` 用于日常 Worker 构建。只有开发环境依赖需要更新时，维护者才在本目录执行以下命令（ARM64 主机）：
```bash
docker build --pull -f docker/Dockerfile.builder --target builder \
  -t registry.chengyistudio.com/cxx/algo-base:GFKD .
docker push registry.chengyistudio.com/cxx/algo-base:GFKD
```
开发镜像从 `algo-base:latest` 构建，安装 Qt5 开发包并编译、测试示例。算法开发者拉取 `algo-base:GFKD` 即可使用，无需维护这份构建文件。
