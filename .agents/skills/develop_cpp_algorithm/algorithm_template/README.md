# Cycore zero-copy algorithm template

This template builds a Flowgraph algorithm plugin against the Cycore SDK. The hot path uses `PortIn<T>`/`PortOut<T>` backed views through the SDK; algorithm code reads and writes ring-buffer memory directly.

## Files

```text
algorithm_template/
├── CMakeLists.txt
├── include/data.h              # sample types and fixed frame shape
├── src/algorithm_block.cpp     # algorithm implementation
└── sdk/include/                # read-only Cycore SDK headers
```

## Data contract

Edit `include/data.h` to declare the stream sample types and matrix shape:

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

Use POD / trivially-copyable sample types only. For structured variable-length data, use a `std::byte` port with the SDK RawBytes helpers instead of putting `std::vector` or `std::string` inside a frame struct.

For radar cubes, keep physical storage as one contiguous stream and use `read_cube(channels, pulses, samples_per_pulse)` / `reserve_cube(...)`. `CubeView` uses sample-major, channel-interleaved indexing, so `view(channel, pulse, sample)` maps to `((pulse * samples_per_pulse + sample) * channels) + channel`. Treat `channels` as a parameter or contract constant; do not bake `16` into algorithm code.

## Work function

The template algorithm uses the zero-copy API:

```cpp
namespace my_block_data = cycore::algorithm::my_block;

bool work(cycore::sdk::Reader<my_block_data::InputSample>& in,
          cycore::sdk::Writer<my_block_data::OutputSample>& out) {
    auto input = in.read_matrix(my_block_data::kInputRows, my_block_data::kInputCols);
    if (!input) return false;

    auto output = out.reserve_matrix(my_block_data::kOutputRows, my_block_data::kOutputCols);
    if (!output) return false;

    // write directly into output ring-buffer memory
    return true;
}
```

Return `true` only after all output memory has been written. The SDK adapter commits output and consumes input together after `work()` returns `true`. Returning `false` or throwing leaves both spans rolled back.

## Build

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The output plugin is `build/my_plugin.so`.
