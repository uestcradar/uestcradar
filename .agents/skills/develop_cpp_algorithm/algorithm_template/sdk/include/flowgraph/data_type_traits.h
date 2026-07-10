#ifndef CYCORE_FLOWGRAPH_DATA_TYPE_TRAITS_H
#define CYCORE_FLOWGRAPH_DATA_TYPE_TRAITS_H

#include <common/data_types.h>
#include <common/i_data_stream.h>

#include <cstddef>
#include <cstdint>

namespace cy::flowgraph {

template <typename T>
struct DataTypeTraits;

template <>
struct DataTypeTraits<cy::common::CS16> {
    static constexpr cy::common::DataType value = cy::common::DataType::CS16;
};

template <>
struct DataTypeTraits<cy::common::CF32> {
    static constexpr cy::common::DataType value = cy::common::DataType::CF32;
};

template <>
struct DataTypeTraits<float> {
    static constexpr cy::common::DataType value = cy::common::DataType::Float32;
};

template <>
struct DataTypeTraits<std::byte> {
    static constexpr cy::common::DataType value = cy::common::DataType::RawBytes;
};

template <>
struct DataTypeTraits<std::uint8_t> {
    static constexpr cy::common::DataType value = cy::common::DataType::UInt8;
};

} // namespace cy::flowgraph

#endif // CYCORE_FLOWGRAPH_DATA_TYPE_TRAITS_H
