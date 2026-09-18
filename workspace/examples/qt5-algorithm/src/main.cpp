#include "worker_support.h"
#include "frame_converter.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        radar_qt_example::WorkerSupport worker;
        worker.start_algorithm();
        while (worker.running()) {
            auto cpi = worker.receive_cpi();
            auto input = radar_qt_example::frame_converter::to_algorithm_frame(cpi);
            worker.write_algorithm_input(input);
            auto result = worker.wait_algorithm_result();
            auto output = radar_qt_example::frame_converter::to_rd_frame(result, cpi);
            worker.publish_rd(output, cpi);
        }
        worker.stop_algorithm();
        return 0;
    } catch (const std::exception& error) {
        qCritical() << "[Worker] FAIL:" << error.what();
        return 1;
    }
}
