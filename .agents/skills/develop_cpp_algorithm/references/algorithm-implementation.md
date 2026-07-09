# 算法核心实现规范 (Algorithm Implementation Guide)

在实现算子的核心 `work` 逻辑时，需遵循以下简明规范与执行顺序。

---

## 1. 配置参数

由于配置字典 `Params` 底层仅支持 `std::int64_t` 变体，为简化代码并避免类型转换，算子内部的整型属性（如通道数、采样点数）**统一使用 `std::int64_t` 进行声明与提取**。

示例：

```cpp
class MyAlgorithm {
public:
    MyAlgorithm(const cycore::sdk::Params& params) {
        // 🟢 所有的整型参数提取，一律用 int64_t 统一提取，无需强转
        num_channels_ = params.get<std::int64_t>("num_channels", data::kDefaultNumChannels);
        fft_size_ = params.get<std::int64_t>("fft_size", data::kDefaultSamplesPerPulse);
    
        if (num_channels_ <= 0 || fft_size_ <= 0) {
            throw std::invalid_argument("Dimensions must be positive");
        }
    }
private:
    std::int64_t num_channels_; 
    std::int64_t fft_size_;
};
```

---

## 2. 读写数据

流图基于 Ring Buffer 原地零拷贝设计，算法在 `work` 中使用前面确定的整型尺寸来申请数据空间。如果锁定失败（数据未准备就绪或下游塞满），直接返回 `false` 触发 SDK 事务回滚：

* **读锁定**：调用 `in.read_matrix(1, fft_size_)`（整型参数传入时自动执行 C++ 隐式类型转换）。
* **写锁定**：调用 `out.reserve_matrix(1, fft_size_)`。

```cpp
bool work(Reader<InputSample>& in, Writer<OutputSample>& out) {
    // 🟢 读写锁定，锁失败则直接退出
    auto input = in.read_matrix(1, fft_size_);
    if (!input) return false;

    auto output = out.reserve_matrix(1, fft_size_);
    if (!output) return false;
  
    // ... 进入数据寻址计算 ...
}
```



---

## 3. 拆分/寻址读取的数据

锁定后的多通道雷达数据在物理内存上默认采用 **通道交织 (Sample-major / Channel-interleaved)** 连续排布：

```text
[ Ch0_S0, Ch1_S0 ... ChN_S0 ] [ Ch0_S1, Ch1_S1 ... ChN_S1 ] ...
└─────── 样点 0 ──────────┘   └─────── 样点 1 ──────────┘
```

为避免解交织带来的临时拷贝开销，推荐在锁定的一维连续空间上通过跨步步长 (Stride) 直接寻址，拆分提取出各通道的样点并写入输出交织槽中：

$$
\text{Index}(n, ch) = n \times N_{channels} + ch
$$

示例：

```cpp
const InputSample* in_ptr = input->data();
OutputSample* out_ptr = output->data();

for (std::int64_t n = 0; n < fft_size_; ++n) {
    for (std::int64_t ch = 0; ch < num_channels_; ++ch) {
        // 🟢 利用跨步偏移公式，拆分提取出 Ch0 .. ChN 对应样点
        const auto& sample = in_ptr[n * num_channels_ + ch];
    
        // 执行计算，并直接写入对应的输出交织位置
        out_ptr[n * num_channels_ + ch] = ...;
    }
}
```

---

## 4. 仿真数据闭环自检规范 (Validation Sandbox)

在第四阶段，严禁使用“前端看波形”这种眼动测试作为通过依据。必须在测试文件中构建纯 C++ 的静态沙盒自检。

### 核心设计规约

1. **直接定义静态测试 Block**：
   在测试 CPP 文件中直接声明继承自 `Block<T>` 的 `SimSource`（仿真信号源）和 `SimSink`（数据校验端）静态对象，实现算法与具体流图的解耦。
2. **理想数据注入与解析断言**：
   * **SimSource**：根据被测算法的数据需求，产生或读取理想的测试输入波形/数据向量（如复单音、线性调频脉冲或特征点阵数据）。
   * **SimSink**：接收算法在 work 后的处理结果，将其与理论解析解（Analytical Solution）进行逐点高精度比对，在指定容差（Epsilon）内执行断言。

### 静态沙盒拓扑与运行模板

```cpp
// 🟢 通用静态仿真信号源
template <typename T>
struct SimSource : public Block<SimSource<T>> {
    PortOut<T> out;
    CY_MAKE_REFLECTABLE(SimSource, out);
    void process_work() {
        auto span = out.reserve(count);
        // 产生测试向量并 commit 提交
        span.commit(count);
    }
};

// 🟢 通用静态数据校验 Sink
template <typename T>
struct SimSink : public Block<SimSink<T>> {
    PortIn<T> in;
    CY_MAKE_REFLECTABLE(SimSink, in);
    void process_work() {
        auto span = in.get(count);
        // 执行理论值比对判定：assert(std::abs(span[i] - expected[i]) < epsilon);
        span.consume(count);
    }
};

int main() {
    Graph graph;
    auto& source = graph.emplace<SimSource<InputType>>("source");
    auto& block = graph.emplace<AlgorithmBlockAdapter<AlgorithmUnderTest>>("algorithm", params);
    auto& sink = graph.emplace<SimSink<OutputType>>("sink");

    graph.connect(source, "out", block, "in", EdgeOptions{capacity});
    graph.connect(block, "out", sink, "in", EdgeOptions{capacity});

    graph.init();
    graph.start();
    graph.work_once();
    graph.stop();
    return 0;
}
```

---

## 5. YAML 完整配置规范

算子在流图 YAML 配置文件中部署时，需满足以下参数与连接配置要求：

### 算子声明与参数配置 (blocks)

必须在 `blocks` 列表中正确注册算子，并配置其所需的整型参数：

```yaml
blocks:
  - id: fft
    type: algorithm.fft_cs16
    plugin: fft_plugin.so
    params:
      fft_size: 1024       # 1024 点 FFT
      num_channels: 16     # 16 通道
```

### 内存连续规避配置 (connections)

流图底层是环形缓冲区，可能会遇到物理内存折返（非连续）。

* **对策**：尽量不要在算子 C++ 代码里写任何复杂的卷绕和拼接逻辑。强制要求在 YAML 配置文件中，将该算子 Connection 的 `capacity` 设为单次读取大小的整数倍。这在物理上能 100% 保证每次申请锁定出的物理内存块一定是绝对连续的一维空间，开发者可以直接用指针偏移跨步寻址。

```yaml
connections:
  - src_block_id: device_source
    src_port: out
    dst_block_id: fft
    dst_port: in
    capacity: 1024         # 设为单次读取大小的整数倍
```
