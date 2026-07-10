# Cycore 脉冲压缩算法插件开发手册 (Pulse Compression)

本手册基于 Cycore SDK，针对**时域滑动互相关脉冲压缩（Pulse Compression）算子**的开发流程、数据契约、类设计、导出宏及隔离编译进行规范说明。

---

## 算子文件结构

```text
pulse_compression/
├── CMakeLists.txt              # CMake 构建定义 (编译为 MODULE)
├── README.md                   # 本开发参考文档
├── include/
│   ├── data.h                  # 声明 I/O 样本类型与三维 Cube 常量
│   └── pulse_compression_algorithm.h  # 算子核心算法类定义 (三大件设计)
└── src/
    └── algorithm.cpp           # 顶层符号导出与注册源文件
```

---

## 第一部分：数据契约 (I/O & Dimensions)

脉冲压缩算子处理的是多通道分布式雷达打流产生的三维雷达数据立方体（Radar Cube）。编辑 `include/data.h` 来声明其数据类型：

```cpp
namespace cycore::algorithm::pulse_compression {

using InputSample = cy::common::CS16;   // 输入样本类型为 16位通道交织有符号复数
using OutputSample = cy::common::CS16;  // 输出样本类型相同

constexpr std::size_t kDefaultNumChannels = 16;       // 默认通道数
constexpr std::size_t kDefaultNumPulses = 64;          // 默认脉冲数
constexpr std::size_t kDefaultSamplesPerPulse = 512;   // 默认单脉冲样点数

} // namespace cycore::algorithm::pulse_compression
```

---

## 第二部分：算法模板类设计 (PulseCompressionAlgorithm)

脉冲压缩算子的 C++ 封装类 `PulseCompressionAlgorithm` 不需要继承任何框架基类，它是纯净的 C++ 非侵入式类。该类包含以下三大件：

### 类定义示例：

```cpp
class PulseCompressionAlgorithm {
public:
    // 1. 构造函数：负责提取参数、执行防御性检验，并预存 256 点 Chirp 发射信号副本
    explicit PulseCompressionAlgorithm(const cycore::sdk::Params& params);

    // 2. 工作函数：执行具体的时域滑动互相关匹配滤波
    bool work(cycore::sdk::Reader<pulse_compression_data::InputSample>& in,
              cycore::sdk::Writer<pulse_compression_data::OutputSample>& out);

private:
    static std::size_t ReadSizeParam(const cycore::sdk::Params& params,
                                     const std::string& key,
                                     std::size_t fallback);

    std::size_t num_channels_;
    std::size_t num_pulses_;
    std::size_t samples_per_pulse_;
    std::vector<ComplexReplica> ref_replica_; // 3. 私有常驻状态：预存的发射 Chirp 共轭参考信号
};
```

### 核心设计与参数预存优化：

* **参数安全提取**：在构造函数内，通过辅助函数 `ReadSizeParam` 调用 `params.get<std::int64_t>()` 提取参数，校验值必须大于 `0`，否则抛出 `std::invalid_argument` 防御崩溃；
* **时域 Chirp 参考信号预存**：为消减运行期 `work` 处理函数中庞大的三角函数（`std::cos`/`std::sin`）开销，我们在构造阶段预先计算 256 点时域 Chirp Replica 的正弦和余弦共轭系数，存储在 `ref_replica_` 常驻数组中。

---

## 第三部分：具体工作函数 (work) 编写规约

算子调度引擎在拉起时循环运行 `work()`。脉冲压缩的 `work` 时域滑动互相关计算流程必须严格按照以下零拷贝规范编写：

```cpp
bool PulseCompressionAlgorithm::work(cycore::sdk::Reader<pulse_compression_data::InputSample>& in,
                                     cycore::sdk::Writer<pulse_compression_data::OutputSample>& out) {
    // 1. 调用 read_cube 一次性读取完整的多通道雷达数据立方体
    auto input = in.read_cube(num_channels_, num_pulses_, samples_per_pulse_);
    if (!input) return false; // 数据流未就绪则退出

    // 2. 从环形缓冲区中一键预留对应的输出 Cube 空间
    auto output = out.reserve_cube(num_channels_, num_pulses_, samples_per_pulse_);
    if (!output) return false; // 预留失败则退出

    // 3. 执行多通道并行时域滑动互相关匹配滤波
    for (std::size_t pulse = 0; pulse < num_pulses_; ++pulse) {
        for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
            for (std::size_t channel = 0; channel < num_channels_; ++channel) {
                double sum_i = 0.0;
                double sum_q = 0.0;
            
                // 滑动互相关计算：使用预存的 256 点本地 Chirp 信号
                for (std::size_t m = 0; m < 256; ++m) {
                    std::size_t idx = sample + m;
                    if (idx < samples_per_pulse_) {
                        auto x = (*input)(channel, pulse, idx); // 寻址输入样点
                        double ref_cos = ref_replica_[m].cos_val;
                        double ref_sin = ref_replica_[m].sin_val;

                        // 共轭复乘累加: x * conj(ref)
                        sum_i += static_cast<double>(x.i) * ref_cos + static_cast<double>(x.q) * ref_sin;
                        sum_q += static_cast<double>(x.q) * ref_cos - static_cast<double>(x.i) * ref_sin;
                    }
                }
            
                // 计算幅值包络并除以积累增益进行归一化
                double amp = std::sqrt(sum_i * sum_i + sum_q * sum_q);
                std::int16_t out_val = static_cast<std::int16_t>(std::round(amp / 256.0));

                // 写入输出环形缓冲区物理地址
                (*output)(channel, pulse, sample) = pulse_compression_data::OutputSample{
                    out_val, 
                    0
                };
            }
        }
    }
    return true; // 返回 true 告知框架提交写入数据并消耗输入
}
```

* **事务性契约**：只在返回 `true` 时，引擎才会统一提交写满的 Cube 数据；若因异常退出或返回 `false`，两端指针回滚，完全保护内存不被错乱污染。

---

## 第四部分：插件导出与注册

导出宏 `CYCORE_EXPORT_ALGORITHM` 必须且只能编写在顶层 C++ 源文件 [src/algorithm.cpp](file:///home/zikun/code/common/uestcradar/cpp/pulse_compression/src/algorithm.cpp) 的全局最底部：

```cpp
CYCORE_EXPORT_ALGORITHM(
    "pulse_compression",                       // 1. 导出插件动态库的文件名 (去除 .so 后缀)
    "algorithm.pulse_compression",             // 2. 注册在流图调度中的算子类型名称 (对齐 YAML 的 blocks.type 字段)
    PulseCompressionAlgorithm,                 // 3. 实现本脉冲压缩逻辑的 C++ 具体类名
    cycore::algorithm::pulse_compression::InputSample,  // 4. 输入通道样本的数据契约
    cycore::algorithm::pulse_compression::OutputSample  // 5. 输出通道样本的数据契约
)
```

---

## 第五部分：CMake 编译配置规范

要让脉冲压缩编译输出为合规的运行时加载插件，[CMakeLists.txt](file:///home/zikun/code/common/uestcradar/cpp/pulse_compression/CMakeLists.txt) 必须声明以下参数：

1. **包含 SDK 头文件目录**：
   ```cmake
   include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../.agents/skills/develop_cpp_algorithm/algorithm_template/sdk/include)
   include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
   ```
2. **声明为 MODULE 插件目标**：
   ```cmake
   add_library(pulse_compression MODULE
       src/algorithm.cpp
   )
   ```
3. **消除 lib 前缀前缀**：
   ```cmake
   set_target_properties(pulse_compression PROPERTIES
       PREFIX ""      # 强行擦除默认生成的 lib 前缀
       SUFFIX ".so"   # 强制指定产物后缀为 .so
   )
   ```

---

## 第六部分：编译构建

本插件支持利用项目专属的自检开发容器进行隔离编译：

### 1. 构建模板专属开发编译镜像 (仅首次需要)

```bash
docker build -t uestcradar-template-build -f ../../.agents/skills/develop_cpp_algorithm/algorithm_template/Dockerfile ../../.agents/skills/develop_cpp_algorithm/algorithm_template
```

### 2. 挂载当前目录在容器中隔离编译

```bash
docker run --rm \                    # 编译完成后自动销毁并清理容器实例，防止垃圾容器残留
  --user "$(id -u):$(id -g)" \       # 以宿主机当前用户的 UID:GID 身份运行，确保生成的编译产物所有权不被 root 锁死
  -v "$(pwd)/../../:/workspace" \    # 将宿主机工程根目录挂载映射到容器内虚拟路径 /workspace
  -w /workspace/cpp/pulse_compression \ # 设置容器内工作路径为脉冲压缩目录
  uestcradar-template-build \        # 使用刚才构建的专属模板编译镜像
  bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
                                     # 启动 Bash 登录环境，一键执行 CMake 配置、Release 优化设置及并行编译
```

### 3. 编译产物获取

* **挂载映射直出**：得益于第二步中的 `-v` 绑定卷参数，编译生成的动态插件库会**自动同步并直接呈现在本地宿主机物理路径 `cpp/pulse_compression/build/pulse_compression.so` 中**，无需手动执行 docker cp 提取。
