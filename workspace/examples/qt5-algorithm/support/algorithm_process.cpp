#include "algorithm_process.h"
#include "vendor/matrixreader.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <QDebug>
#include <QUuid>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>

namespace radar_qt_example {
void ChildProcess::startAlgorithm(const QString& executable) {
    expected_parent_pid_ = getpid();
    QProcess::start(executable, QStringList{});
}
void ChildProcess::setupChildProcess() {
    // PID 1 is a valid Worker parent inside a container. Detect reparenting
    // against the PID captured before fork, including death before prctl.
    if (prctl(PR_SET_PDEATHSIG,SIGTERM)!=0 || getppid()!=expected_parent_pid_) _exit(125);
}
AlgorithmProcess::AlgorithmProcess(QString exe,QString dir,int timeout)
    : executable_(std::move(exe)),workdir_(std::move(dir)),
      key_("uestcradar_gfkd_"+QUuid::createUuid().toString(QUuid::Id128)),timeout_ms_(timeout) {}
AlgorithmProcess::~AlgorithmProcess() { stop(); }
void AlgorithmProcess::start() {
    for (const auto* csv : {"subband_filter_32x32.csv","subband_filter_64x64.csv"}) {
        if (!QFile::exists(workdir_+"/"+csv)) throw std::runtime_error("Missing algorithm filter CSV");
    }
    input_=std::make_unique<InputChannel>(key_);
    output_=std::make_unique<MatrixRingBuffer>("MatrixBuffer",32*1024*1024);
    output_->clear();
    auto env=QProcessEnvironment::systemEnvironment();
    env.insert("GFKD_INPUT_SHM_KEY",key_);
    process_.setProcessEnvironment(env);
    process_.setWorkingDirectory(workdir_);
    process_.setProcessChannelMode(QProcess::ForwardedChannels);
    process_.startAlgorithm(executable_);
    if (!process_.waitForStarted(30000)) throw std::runtime_error("Cannot start algorithm executable");
}
void AlgorithmProcess::check_process() {
    process_.waitForReadyRead(1);
    if (process_.state()==QProcess::NotRunning)
        throw std::runtime_error("Algorithm exited; see container logs");
}
void AlgorithmProcess::write(const QByteArray& input) {
    QElapsedTimer timer; timer.start();
    while (!input_->write(input)) {
        check_process();
        if (timer.elapsed()>timeout_ms_) throw std::runtime_error("Algorithm input timeout");
        QThread::msleep(1);
    }
}
AlgorithmResult AlgorithmProcess::wait_result() {
    QElapsedTimer timer; timer.start();
    AlgorithmResult result;
    while (true) {
        check_process();
        if (MatrixReader::getLatestMatrix(result.frequencies,result.ranges,result.values)) {
            qInfo() << "[Algorithm] elapsed_ms=" << timer.elapsed()
                    << "shape=" << result.ranges << "x" << result.frequencies;
            return result;
        }
        if (timer.elapsed()>timeout_ms_) throw std::runtime_error("Algorithm result timeout");
        QThread::msleep(2);
    }
}
void AlgorithmProcess::stop() {
    if (process_.state()!=QProcess::NotRunning) {
        process_.terminate();
        if (!process_.waitForFinished(3000)) { process_.kill(); process_.waitForFinished(3000); }
    }
    output_.reset(); input_.reset();
}
}
