#pragma once
#include "frame_converter.h"
#include "input_channel.h"
#include "vendor/matrixringbuffer.h"
#include <QProcess>
#include <memory>
#include <sys/types.h>

namespace radar_qt_example {
// Linux parent-death signal also stops the algorithm if Worker is killed while SDK read blocks.
class ChildProcess : public QProcess {
public:
    void startAlgorithm(const QString& executable);
protected:
    void setupChildProcess() override;
private:
    pid_t expected_parent_pid_{};
};
class AlgorithmProcess {
public:
    AlgorithmProcess(QString executable, QString workdir, int timeout_ms);
    ~AlgorithmProcess();
    void start();
    void write(const QByteArray& input);
    AlgorithmResult wait_result();
    void stop();
private:
    void check_process();
    QString executable_,workdir_,key_;
    int timeout_ms_;
    ChildProcess process_;
    std::unique_ptr<InputChannel> input_;
    std::unique_ptr<MatrixRingBuffer> output_;
};
}
