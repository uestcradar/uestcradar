#pragma once
#include "frame_converter.h"
#include "input_channel.h"
#include "vendor/matrixringbuffer.h"
#include <QProcess>
#include <QFile>
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
    AlgorithmProcess(QString executable, QString workdir, QString output_dir, int timeout_ms);
    ~AlgorithmProcess();
    void start();
    void write(const QByteArray& input);
    AlgorithmResult wait_result();
    void stop();
private:
    void check_process();
    void collect_logs();
    QString executable_,workdir_,output_dir_,key_;
    int timeout_ms_;
    ChildProcess process_;
    std::unique_ptr<InputChannel> input_;
    std::unique_ptr<MatrixRingBuffer> output_;
    QFile log_;
};
}
