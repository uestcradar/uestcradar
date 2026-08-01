#pragma once

#include <raw_frame.hpp>

#include <memory>
#include <span>

#if defined(_WIN32)
#  if defined(CYCOMM_SDK_BUILD)
#    define CYCOMM_SDK_API __declspec(dllexport)
#  else
#    define CYCOMM_SDK_API __declspec(dllimport)
#  endif
#else
#  define CYCOMM_SDK_API __attribute__((visibility("default")))
#  define CYCOMM_SDK_LOCAL __attribute__((visibility("hidden")))
#endif

#if defined(_WIN32)
#  define CYCOMM_SDK_LOCAL
#endif

namespace uestcradar {

template <class Frame>
class Input;

template <class Frame>
class Output;

class CYCOMM_SDK_API RawFrame final {
public:
    RawFrame(RawFrame&& other) noexcept;
    RawFrame& operator=(RawFrame&& other) noexcept;
    RawFrame(const RawFrame&) = delete;
    RawFrame& operator=(const RawFrame&) = delete;
    ~RawFrame();

    [[nodiscard]] Envelope& envelope();
    [[nodiscard]] const Envelope& envelope() const;
    [[nodiscard]] std::span<std::byte> payload_span();
    [[nodiscard]] std::span<const std::byte> payload_span() const;

private:
    struct Impl;
    explicit CYCOMM_SDK_LOCAL RawFrame(
        std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;

    friend class Input<RawFrame>;
    friend class Output<RawFrame>;
};

template <>
class CYCOMM_SDK_API Input<RawFrame> final {
public:
    Input();
    Input(Input&& other) noexcept;
    Input& operator=(Input&& other) noexcept;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    ~Input();

    [[nodiscard]] RawFrame read();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

template <>
class CYCOMM_SDK_API Output<RawFrame> final {
public:
    Output();
    Output(Output&& other) noexcept;
    Output& operator=(Output&& other) noexcept;
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    ~Output();

    [[nodiscard]] RawFrame create(const Envelope& envelope);
    void write(RawFrame&& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace uestcradar
