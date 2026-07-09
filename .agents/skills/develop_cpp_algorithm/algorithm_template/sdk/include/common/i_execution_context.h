#ifndef CYCORE_DU_COMMON_I_EXECUTION_CONTEXT_H
#define CYCORE_DU_COMMON_I_EXECUTION_CONTEXT_H

#include <common/i_data_stream.h>

#include <memory>
#include <string>

namespace cy::common {

class IExecutionContext {
public:
    virtual ~IExecutionContext() = default;

    virtual std::shared_ptr<IDataReader> get_reader(const std::string& ref) const = 0;
    virtual std::shared_ptr<IDataWriter> get_writer(const std::string& ref) const = 0;
};

} // namespace cy::common

#endif // CYCORE_DU_COMMON_I_EXECUTION_CONTEXT_H
