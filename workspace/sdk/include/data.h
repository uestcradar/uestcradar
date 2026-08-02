#pragma once

#include <sdk.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>

namespace uestcradar {

struct ComplexInt16 {
    std::int16_t i{};
    std::int16_t q{};
};

struct ComplexFloat32 {
    float i{};
    float q{};
};

template <class T>
class Array2D {
public:
    Array2D(T* values, std::size_t rows, std::size_t columns) noexcept
        : values_(values), rows_(rows), columns_(columns) {}

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }

    [[nodiscard]] std::span<T> operator[](std::size_t row) const;
    [[nodiscard]] std::span<T> values() const noexcept {
        return {values_, rows_ * columns_};
    }

private:
    T* values_;
    std::size_t rows_;
    std::size_t columns_;
};

struct IQMetadata {
    std::uint32_t channel_count{};
    std::uint32_t samples_per_channel{};
    double sample_rate_hz{};
    double center_frequency_hz{};
};

struct PulseCompressionMetadata {
    std::uint32_t channel_count{};
    std::uint32_t range_bin_count{};
    std::uint32_t pulse_index{};
    std::uint32_t pulses_per_cpi{};
    double range_resolution_m{};
};

struct RDMetadata {
    std::uint32_t channel_index{};
    std::uint32_t range_bin_count{};
    std::uint32_t doppler_bin_count{};
    double range_resolution_m{};
    double velocity_resolution_mps{};
};

class CYCOMM_SDK_API IQFrame final {
public:
    IQFrame(IQFrame&& other) noexcept;
    IQFrame& operator=(IQFrame&& other) noexcept;
    ~IQFrame();

    [[nodiscard]] IQMetadata metadata() const;
    [[nodiscard]] Array2D<ComplexInt16> data();
    [[nodiscard]] Array2D<const ComplexInt16> data() const;

private:
    struct Impl;
    explicit IQFrame(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] const void* sdk_trace_context() const;
    std::unique_ptr<Impl> impl_;
    friend class Input<IQFrame>;
    friend class Output<IQFrame>;
    template <class>
    friend class Output;
};

class CYCOMM_SDK_API PulseCompressionFrame final {
public:
    PulseCompressionFrame(PulseCompressionFrame&& other) noexcept;
    PulseCompressionFrame& operator=(PulseCompressionFrame&& other) noexcept;
    ~PulseCompressionFrame();

    [[nodiscard]] PulseCompressionMetadata metadata() const;
    [[nodiscard]] Array2D<ComplexFloat32> data();
    [[nodiscard]] Array2D<const ComplexFloat32> data() const;

private:
    struct Impl;
    explicit PulseCompressionFrame(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] const void* sdk_trace_context() const;
    std::unique_ptr<Impl> impl_;
    friend class Input<PulseCompressionFrame>;
    friend class Output<PulseCompressionFrame>;
    template <class>
    friend class Output;
};

class CYCOMM_SDK_API RDFrame final {
public:
    RDFrame(RDFrame&& other) noexcept;
    RDFrame& operator=(RDFrame&& other) noexcept;
    ~RDFrame();

    [[nodiscard]] RDMetadata metadata() const;
    [[nodiscard]] Array2D<float> data();
    [[nodiscard]] Array2D<const float> data() const;

private:
    struct Impl;
    explicit RDFrame(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] const void* sdk_trace_context() const;
    std::unique_ptr<Impl> impl_;
    friend class Input<RDFrame>;
    friend class Output<RDFrame>;
    template <class>
    friend class Output;
};

#define UESTCRADAR_DECLARE_INPUT(FrameType)                            \
    template <>                                                       \
    class CYCOMM_SDK_API Input<FrameType> final {                     \
    public:                                                           \
        Input();                                                      \
        Input(Input&& other) noexcept;                                \
        Input& operator=(Input&& other) noexcept;                     \
        Input(const Input&) = delete;                                 \
        Input& operator=(const Input&) = delete;                      \
        ~Input();                                                     \
        [[nodiscard]] FrameType read();                               \
    private:                                                          \
        struct Impl;                                                  \
        std::unique_ptr<Impl> impl_;                                  \
    }

#define UESTCRADAR_DECLARE_OUTPUT(FrameType, MetadataType)            \
    template <>                                                       \
    class CYCOMM_SDK_API Output<FrameType> final {                    \
    public:                                                           \
        Output();                                                     \
        Output(Output&& other) noexcept;                              \
        Output& operator=(Output&& other) noexcept;                   \
        Output(const Output&) = delete;                               \
        Output& operator=(const Output&) = delete;                    \
        ~Output();                                                    \
        [[nodiscard]] FrameType create(const MetadataType& metadata); \
        template <class ParentFrame>                                  \
        [[nodiscard]] FrameType create(                               \
            const MetadataType& metadata, const ParentFrame& parent) { \
            return create_linked(                                     \
                metadata, parent.sdk_trace_context());                 \
        }                                                             \
        void write(FrameType&& frame);                                \
    private:                                                          \
        [[nodiscard]] FrameType create_linked(                        \
            const MetadataType& metadata, const void* trace_context); \
        struct Impl;                                                  \
        std::unique_ptr<Impl> impl_;                                  \
    }

#define UESTCRADAR_CONTRACT(Name, FrameType, MetadataType) \
    UESTCRADAR_DECLARE_INPUT(FrameType);                    \
    UESTCRADAR_DECLARE_OUTPUT(FrameType, MetadataType);
#include <contract_catalog.def>
#undef UESTCRADAR_CONTRACT

#undef UESTCRADAR_DECLARE_INPUT
#undef UESTCRADAR_DECLARE_OUTPUT

template <class T>
std::span<T> Array2D<T>::operator[](std::size_t row) const {
    if (row >= rows_) {
        throw std::out_of_range("data row is out of range");
    }
    return {values_ + row * columns_, columns_};
}

}  // namespace uestcradar
