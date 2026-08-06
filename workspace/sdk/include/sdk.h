#pragma once

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

}  // namespace uestcradar
