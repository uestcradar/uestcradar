#pragma once

#include <cstddef>
#include <common/data_types.h>
#include <common/span.h>

namespace cy::common {

enum class DataType {
    Int16,
    Int32,
    Float32,
    UInt8,
    CS16,     // 16位有符号复数 (雷达常见 IQ)
    CF32,     // 32位单精度复数 (雷达常见 IQ)
    RawBytes  // 原始无类型二进制流
};

class IDataReader {
public:
    virtual ~IDataReader() = default;
    virtual bool is_active() const = 0;

    virtual DataType get_data_type() const = 0;
    virtual std::size_t get_element_size() const = 0; // 每个数据元素的物理字节大小

    /// 💡 读接口：仅接受字节缓冲区视图
    /// @param buffer 可写的字节缓冲区视图 (std::byte 类型精准表达原始字节)
    /// @return 实际成功读取的字节数，<=0 表示超时、错误或结束
    virtual int read(Span<std::byte> buffer, long timeout_us = 100000) = 0;
};

class IDataWriter {
public:
    virtual ~IDataWriter() = default;
    virtual bool is_active() const = 0;

    virtual DataType get_data_type() const = 0;
    virtual std::size_t get_element_size() const = 0;

    /// 💡 写接口：仅接受只读字节缓冲区视图
    /// @return 实际成功写入的字节数，<=0 表示出错
    virtual int write(Span<const std::byte> buffer, long timeout_us = 100000) = 0;
};

} // namespace cy::common
