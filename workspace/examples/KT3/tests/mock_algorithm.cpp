#include "vendor/matrixringbuffer.h"
#include <QCoreApplication>
#include <QThread>
#include <QDataStream>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto mode=qgetenv("GFKD_TEST_MODE");
    if (mode=="exit") return 7;
    if (mode=="timeout") { QThread::sleep(30); return 0; }
    QSharedMemory input(QString::fromLocal8Bit(qgetenv("GFKD_INPUT_SHM_KEY")));
    if (!input.attach()) return 8;
    while (true) {
        input.lock();auto* h=static_cast<quint32*>(input.data());
        const bool ready=h[2]>0;
        if (ready) h[2]=h[3]=0;
        input.unlock();if (ready) break;QThread::msleep(1);
    }
    MatrixRingBuffer output("MatrixBuffer",32*1024*1024);
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(0xAA55AA55) << qint32(2) << qint32(3);
    for (double v:{1.,4.,2.,5.,3.,6.}) stream << v;
    output.write(bytes);
    QThread::sleep(30);
}
