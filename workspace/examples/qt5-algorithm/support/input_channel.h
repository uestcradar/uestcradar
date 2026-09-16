#pragma once
#include "rd_contract.hpp"
#include <QSharedMemory>
#include <QByteArray>
#include <cstring>
#include <stdexcept>

namespace radar_qt_example {
class InputChannel {
public:
    explicit InputChannel(const QString& key) : memory_(key) {
        if (!memory_.create(kInputShmBytes))
            throw std::runtime_error("Cannot create algorithm input: " + memory_.errorString().toStdString());
        if (!memory_.lock()) throw std::runtime_error("Cannot initialize input lock");
        auto* h=static_cast<quint32*>(memory_.data());
        h[0]=0x47504631u; h[1]=kInputShmBytes-16; h[2]=h[3]=0;
        memory_.unlock();
    }
    bool write(const QByteArray& bytes) {
        if (bytes.isEmpty() || std::size_t(bytes.size())>kInputShmBytes-16)
            throw std::invalid_argument("Algorithm input exceeds shared memory capacity");
        if (!memory_.lock()) throw std::runtime_error("Cannot lock algorithm input");
        auto* h=static_cast<quint32*>(memory_.data());
        if (h[2]!=0) { memory_.unlock(); return false; }
        std::memcpy(static_cast<char*>(memory_.data())+16,bytes.constData(),bytes.size());
        h[2]=bytes.size(); h[3]=0;
        memory_.unlock();
        return true;
    }
private:
    QSharedMemory memory_;
};
}
