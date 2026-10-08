#include "worker_support.h"
#include <QDebug>
namespace radar_qt_example {
static QString environment(const char* key,const char* fallback) {
    const auto value=qgetenv(key);return value.isEmpty()?QString(fallback):QString::fromLocal8Bit(value);
}
WorkerSupport::WorkerSupport()
    : algorithm_(environment("GFKD_EXECUTABLE","/app/algorithm/GFKD_V1_ARM"),
                 environment("GFKD_WORKDIR","/app/algorithm"),
                 environment("GFKD_TIMEOUT_MS","600000").toInt()),
      limit_(environment("WORKER_FRAMES","0").toULongLong()) {}
CpiBuffer WorkerSupport::receive_cpi() {
    CpiBuffer cpi;
    do {
        auto pulse=input_.read();
        cpi.push(pulse.metadata(),pulse.data()[0]);
        if (cpi.ready()) parent_.emplace(std::move(pulse));
    } while (!cpi.ready());
    return cpi;
}
void WorkerSupport::publish_rd(const RdOutput& result,const CpiBuffer&) {
    auto frame=output_.create(result.metadata,*parent_);
    std::copy(result.values.begin(),result.values.end(),frame.data().values().begin());
    output_.write(std::move(frame));parent_.reset();
    qInfo() << "[Worker] completed=" << ++completed_ << "shape=" << result.metadata.range_bin_count
            << "x" << result.metadata.doppler_bin_count;
}
}
