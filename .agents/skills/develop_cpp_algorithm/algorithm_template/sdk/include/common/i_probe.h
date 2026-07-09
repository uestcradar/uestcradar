#ifndef CYCORE_DU_COMMON_I_PROBE_H
#define CYCORE_DU_COMMON_I_PROBE_H

#include <common/i_data_stream.h>
#include <common/span.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace cy::common {

class IProbe {
public:
    virtual ~IProbe() = default;

    virtual const std::string& topic() const noexcept = 0;
    virtual DataType data_type() const noexcept = 0;
    virtual std::size_t element_size() const noexcept = 0;
    virtual std::size_t frame_size() const noexcept = 0;

    virtual void request() noexcept = 0;
    virtual std::size_t peek_latest(Span<std::byte> buffer) const = 0;
};

class IProbeProvider {
public:
    virtual ~IProbeProvider() = default;

    virtual std::shared_ptr<IProbe> GetProbe(const std::string& topic) const = 0;
    virtual std::vector<std::string> ListProbeTopics() const = 0;
};

} // namespace cy::common

#endif // CYCORE_DU_COMMON_I_PROBE_H
