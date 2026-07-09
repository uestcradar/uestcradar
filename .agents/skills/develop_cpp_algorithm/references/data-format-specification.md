# 输入输出数据格式规范

在 Cycore 架构中，由于传输的数据格式已高度固定，新算子开发在格式制定上仅有极简约束。

---

## 1. 强类型复用规范 

新算子的输入输出样本类型**禁止自创**，必须直接复用项目已有的标准类型：

* **常用固定类型**：引入 `<common/data_types.h>`，直接使用 `cy::common::CS16` (复数 short，雷达流主格式) 或 `cy::common::CF32` (复数 float，频域高精流)。
* **警告**：自创类型即使内存大小一致，也会因 C++ 强类型检查导致 Port 连接失败。

---

## 2. 变长数据的 RawBytes 平坦化规划

对于“点迹/航迹列表”等变长动态数据，流图无法使用动态容器（如 `std::vector`），必须统一通过 `std::byte` 类型的 **RawBytes 端口** 传输，并遵循以下平坦头紧凑排布布局：

### 内存布局：

```text
[ elem_count (4B) | elem_size (4B) ] [ 元素 0 ] [ 元素 1 ] ... [ 元素 N-1 ]
  └─────── RawArrayHeader ────────┘   └────────────── 元素数据 ──────────────┘
```

* **包头结构**：
  ```cpp
  struct alignas(8) RawArrayHeader {
      std::uint32_t elem_count; // 元素数量
      std::uint32_t elem_size;  // 单个元素字节大小
  };
  ```
* **读取模式**：
  下游算子直接通过 SDK 提供的类型安全模板指针进行非拷贝读取：
  ```cpp
  struct TrackPoint { float range; float doppler; };
  auto points = in.read_raw_array<TrackPoint>(); // 自动校验包头对齐与大小
  ```
