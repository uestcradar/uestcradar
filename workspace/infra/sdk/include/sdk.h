#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#if defined(_WIN32)
#  if defined(CYCOMM_SDK_BUILD)
#    define CYCOMM_SDK_API __declspec(dllexport)
#  else
#    define CYCOMM_SDK_API __declspec(dllimport)
#  endif
#else
#  define CYCOMM_SDK_API __attribute__((visibility("default")))
#endif

namespace uestcradar {

template <class DataFrame>
class Input;

template <class DataFrame>
class Output;

// Read-only complete wire frame. Views expire when this lease is destroyed.
class CYCOMM_SDK_API RawFrame final {
public:
    RawFrame(RawFrame&&) noexcept;
    RawFrame& operator=(RawFrame&&) noexcept;
    ~RawFrame();
    [[nodiscard]] std::span<const std::byte> bytes() const;

private:
    struct Impl;
    explicit RawFrame(std::unique_ptr<Impl>) noexcept;
    std::unique_ptr<Impl> impl_;
    friend class Input<RawFrame>;
};

template <>
class CYCOMM_SDK_API Input<RawFrame> final {
public:
    Input(std::uint64_t type_id, std::uint32_t type_version);
    Input(Input&&) noexcept;
    Input& operator=(Input&&) noexcept;
    ~Input();
    // Empty means would_block only. Shutdown/corruption throws.
    // Single reader; release the previous RawFrame before reading again.
    [[nodiscard]] std::optional<RawFrame> try_read();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace uestcradar
