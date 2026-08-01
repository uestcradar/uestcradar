#pragma once

#include <memory>

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

class CYCOMM_SDK_API Frame {
public:
    Frame(Frame&& other) noexcept;
    Frame& operator=(Frame&& other) noexcept;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    ~Frame();

protected:
    struct Impl;
    explicit Frame(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] Impl& impl();
    [[nodiscard]] const Impl& impl() const;

private:
    std::unique_ptr<Impl> impl_;

    template <class>
    friend class Input;
    template <class>
    friend class Output;
};

}  // namespace uestcradar
