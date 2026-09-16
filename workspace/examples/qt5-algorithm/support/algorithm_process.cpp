#include "algorithm_process.h"
#include "vendor/matrixreader.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QThread>
#include <QDebug>
#include <QUuid>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>

namespace radar_qt_example {
void ChildProcess::setupChildProcess() {
    if (prctl(PR_SET_PDEATHSIG,SIGTERM)!=0 || getppid()==1) _exit(125);
}
AlgorithmProcess::AlgorithmProcess(QString exe,QString dir,QString out,int timeout)
    : executable_(std::move(exe)),workdir_(std::move(dir)),output_dir_(std::move(out)),
      key_("uestcradar_gfkd_"+QUuid::createUuid().toString(QUuid::Id128)),timeout_ms_(timeout) {}
AlgorithmProcess::~AlgorithmProcess() { stop(); }
void AlgorithmProcess::start() {
    QDir().mkpath(output_dir_);
    log_.setFileName(output_dir_+"/algorithm.log");
    if (!log_.open(QIODevice::WriteOnly|QIODevice::Truncate)) throw std::runtime_error("Cannot open algorithm log");
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
    process_.setProcessChannelMode(QProcess::MergedChannels);
    process_.start(executable_,QStringList{});
    if (!process_.waitForStarted(30000)) throw std::runtime_error("Cannot start algorithm executable");
}
void AlgorithmProcess::collect_logs() {
    const auto data=process_.readAll();
    if (!data.isEmpty()) { log_.write(data); log_.flush(); }
}
void AlgorithmProcess::check_process() {
    process_.waitForReadyRead(1);
    collect_logs();
    if (process_.state()==QProcess::NotRunning)
        throw std::runtime_error("Algorithm exited; see algorithm.log");
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
    collect_logs();
    output_.reset(); input_.reset();
}
}
