    rm -rf build && docker run --rm --user "$(id -u):$(id -g)" -v "$(pwd):/workspace" -w /workspace uestcradar-template-build bash -lc 

# Cycore 算法开发模板

本模板基于 Cycore SDK 构建了一个算法开发模板。

## 模板结构

```text
algorithm_template/
├── CMakeLists.txt
├── include/data.h              # 示例数据类型与固定数据帧维度定义
├── src/algorithm_block.cpp     # 算法具体实现
└── sdk/include/                # 只读的 Cycore SDK 头文件
```

## 数据契约

编辑 `include/data.h` 来声明流式数据类型与矩阵维度：

```cpp
namespace cycore::algorithm::my_block {

using InputSample = float;
using OutputSample = float;

constexpr std::size_t kInputRows = 1;
constexpr std::size_t kInputCols = 1024;
constexpr std::size_t kOutputRows = 1;
constexpr std::size_t kOutputCols = 1024;

} // namespace cycore::algorithm::my_block
```

请仅使用 POD（平坦旧数据）/ 平凡可复制（Trivially-copyable）的样本数据类型。对于结构化的变长数据，请使用带 SDK RawBytes 辅助程序的 `std::byte` 端口，而不要在帧结构体中嵌入 `std::vector` 或 `std::string`。

对于雷达数据立方体（Radar Cube），请将物理存储保持为一整个连续流，并使用 `read_cube(channels, pulses, samples_per_pulse)` / `reserve_cube(...)`。`CubeView` 采用样本主序、通道交织寻址方式，因此 `view(channel, pulse, sample)` 将映射至 `((pulse * samples_per_pulse + sample) * channels) + channel`。请将 `channels` 视为一个参数或契约常量，严禁在算法代码中硬编码 `16`。

## 算法开发规范

开发算法需要将算法用一个类描述

### 模板类示例：

必须包含

1.构造函数

2.工作函数

```cpp
class MyAlgorithm {
public:
    // 1. 构造函数：负责提取参数并校验
    explicit MyAlgorithm(const cycore::sdk::Params& params)
        : factor_(params.get<double>("factor", 1.0)) {
        if (factor_ < -1.0e9 || factor_ > 1.0e9) {
            throw std::invalid_argument("factor is out of supported range");
        }
    }

    // 2. 工作函数：执行具体的信号处理逻辑
    bool work(cycore::sdk::Reader<my_block_data::InputSample>& in,
              cycore::sdk::Writer<my_block_data::OutputSample>& out);

private:
    double factor_ = 1.0; // 3. 私有常驻参数
};
```

### 工作函数

算子在被流图调度拉起时，会循环触发 `work()` 函数。

#### 工作函数示例代码：

```cpp
bool MyAlgorithm::work(cycore::sdk::Reader<my_block_data::InputSample>& in,
                       cycore::sdk::Writer<my_block_data::OutputSample>& out) {
  	// 1. 数据读取 (以 read_matrix 二维矩阵读取为例)
    auto input = in.read_matrix(my_block_data::kInputRows, my_block_data::kInputCols);
    if (!input) return false;
  
	// 2. 输出空间预留 (以 reserve_matrix 二维矩阵预留为例)
    auto output = out.reserve_matrix(my_block_data::kOutputRows, my_block_data::kOutputCols);
    if (!output) return false;
  
	// 3. 原地零拷贝算法计算与写入
    for (std::size_t row = 0; row < input->rows(); ++row) {
        for (std::size_t col = 0; col < input->cols(); ++col) {
            (*output)(row, col) = (*input)(row, col) * static_cast<my_block_data::OutputSample>(factor_);
        }
    }
    return true; // 成功返回 true，自动执行提交与消耗；失败返回 false 会发生指针回滚。
}
```

编写具体的 `work` 逻辑时，可以用以下函数读取数据。

#### SDK 提供的读取与写入预留接口

##### (1) 四种读取方法 (`cycore::sdk::Reader<T>`)

* **`read(count)`**：物理读取指定长度（`count`）的数据切片（返回 `ArrayView`）。若缓冲区折返不连续，将抛出运行期异常；
* **`read_available(max_count)`**：自适应读取当前最长可读的连续切片（不超过 `max_count`），绝对不会因缓冲区环形折返而报错；
* **`read_matrix(rows, cols)`**：读取一个二维矩阵视图（`MatrixView`，元素数为 `rows * cols`），提供矩阵寻址支持；
* **`read_cube(channels, pulses, samples_per_pulse)`**：读取一个三维数据立方体视图（`CubeView`），提供雷达数据交织快速索引。

##### (2) 四种写入/预留方法 (`cycore::sdk::Writer<T>`)

* **`reserve(count)`**：申请预留指定长度（`count`）的连续输出缓冲区（返回 `ArrayView`）。若剩余空间不足，将抛出异常；
* **`reserve_available(max_count)`**：自适应申请当前最长可写的连续输出缓冲区，防止折返不连续而发生报错；
* **`reserve_matrix(rows, cols)`**：申请预留一个二维矩阵视图（`MatrixView`），供您直接以矩阵索引方式写入数据；
* **`reserve_cube(channels, pulses, samples_per_pulse)`**：申请预留一个三维雷达数据立方体视图（`CubeView`），供雷达数据流直接写入。

### 算法导出与注册

为了将编写的算法编译导出成动态库插件给 Cycore 流图框架正确使用，必须在顶层C++源文件的尾部使用 `CYCORE_EXPORT_ALGORITHM` 宏执行符号导出与注册：

```cpp
CYCORE_EXPORT_ALGORITHM(
    "my_plugin",                               // 1. 导出动态库插件名称 (对应生成的 my_plugin.so 文件名，去除 .so 后缀)
    "algorithm.my_block",                      // 2. 算子注册类型名称 (对应目标 YAML 拓扑配置文件中 blocks.type 字段)
    MyAlgorithm,                               // 3. 实现该算法逻辑的具体 C++ 类名
    cycore::algorithm::my_block::InputSample,  // 4. 输入通道样本的数据类型
    cycore::algorithm::my_block::OutputSample  // 5. 输出通道样本的数据类型
)
```

流图在拉起时会动态 `dlopen` 加载相应的插件并查找符号。若此处注册的“动态库插件名”或“注册类型名”与 YAML 拓扑配置不吻合，系统将报错中断。

## 编译构建规范

### 1.CMakelist规范

为了让 C++ 算子代码成功被编译为能够被 Cycore 流图调度引擎动态加载的共享插件模块，必须在 [CMakeLists.txt](CMakeLists.txt) 中声明以下核心配置：

#### (1)包含 SDK 依赖目录

在编译路径中添加 SDK 头文件目录，从而让编译器能够正确解析插件注册头文件依赖：

```cmake
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/sdk/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
```

#### (2)将顶层源文件声明为插件库

算子插件属于运行时动态加载模块，在 CMake 中必须声明为 **`MODULE`** 库，而非普通的共享链接库 `SHARED`：

```cmake
add_library(my_plugin MODULE
    src/algorithm_block.cpp
)
```

#### (3)去除默认 `lib` 前缀 (与 YAML 契约对齐)

在 Linux 环境下，共享库默认会被 CMake 自动加上 `lib` 前缀（如 `libmy_plugin.so`）。我们必须强制通过以下 target 属性设置**将 `lib` 前缀擦除**，让其直接输出为 `my_plugin.so`，以确保其能与流图 YAML 配置文件中声明的 `plugin` 文件名精确对齐：

```cmake
set_target_properties(my_plugin PROPERTIES
    PREFIX ""	//强制约定产物前缀为空
    SUFFIX ".so"//后缀名为so文件
)
```

### 2.编译流程

本模板支持利用同目录下的专属 `Dockerfile` 进行容器隔离编译：

#### (1)构建模板专属开发编译镜像 (仅首次需要)

在模板根目录下执行：

```bash
docker build -t uestcradar-template-build . \	#构建的镜像名称
      -f Dockerfile.build_cross					#使用的dockerfile
```

#### (2)挂载当前目录在容器中隔离编译

挂载模板文件夹并在隔离容器中编译，直接输出动态插件库：

```bash
docker run --rm \                    # 编译完成后自动销毁并清理容器实例，防止垃圾容器残留
  --user "$(id -u):$(id -g)" \       # 以宿主机当前用户的 UID:GID 身份运行，确保生成的编译产物所有权不被 root 锁死
  -v "$(pwd):/workspace" \           # 将宿主机当前目录挂载映射到容器内虚拟路径 /workspace
  -w /workspace \                    # 设置容器内工作路径为挂载的 /workspace 目录
  uestcradar-template-build \        # 使用刚才构建的专属模板编译镜像
  bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
                                     # 启动 Bash 登录环境，一键执行 CMake 配置、Release 优化设置及并行编译
```

```bash
rm -rf build
docker run --rm --user "$(id -u):$(id -g)" -v "$(pwd):/workspace" -w /workspace uestcradar-template-build bash -lc "cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc)"
```

#### (3)编译产物获取

* **挂载映射直出**：得益于第二步中 `-v "$(pwd):/workspace"` 挂载参数，编译生成的插件会**自动同步并直接呈现在宿主机本地工程根目录下的 `build/` 文件夹中**，无需执行 `docker cp` 命令从容器内手动提取。
* **宿主机物理路径**：`build/my_plugin.so`。
