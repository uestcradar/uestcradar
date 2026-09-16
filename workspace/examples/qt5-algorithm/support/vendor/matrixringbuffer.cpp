#include "matrixringbuffer.h"
#include <QDebug>
#include <QDataStream>
#include <cstring>

MatrixRingBuffer::MatrixRingBuffer(const QString& key, int size)
    : m_sharedMem(key)
    , m_mutex(key + "_mutex", 1)   // 信号量初始值 1
    , m_dataOffset(32)
{
    if (!m_sharedMem.create(size, QSharedMemory::ReadWrite)) {
        if (!m_sharedMem.attach(QSharedMemory::ReadWrite)) {
            qFatal("MatrixRingBuffer: failed to create/attach shared memory");
        }
    }

    // 首次使用则初始化头部
    if (m_sharedMem.lock()) {
        if (magic() != MAGIC_NUMBER) {
            magic() = MAGIC_NUMBER;
            writePos() = 0;
            readPos() = 0;
            dataSize() = size - m_dataOffset;
        }
        m_sharedMem.unlock();
    }
}

MatrixRingBuffer::~MatrixRingBuffer()
{
    m_sharedMem.detach();
}

bool MatrixRingBuffer::write(const QByteArray& record)
{
    const int recLen = record.size();
    if (recLen > dataSize()) {
        qWarning() << "MatrixRingBuffer::write: record too large";
        return false;
    }

    m_mutex.acquire();

    int bufSize = dataSize();
    int wPos = writePos();
    int rPos = readPos();

    // 计算可用连续空间（环形）
    int freeSpace = (rPos - wPos - 1 + bufSize) % bufSize;
    if (freeSpace < recLen) {
        // 空间不足，覆盖最旧数据：移动读指针
        rPos = (wPos + recLen) % bufSize;
        readPos() = rPos;
    }

    char* base = static_cast<char*>(m_sharedMem.data()) + m_dataOffset;
    int first = qMin(recLen, bufSize - wPos);
    ::memcpy(base + wPos, record.constData(), first);
    if (first < recLen) {
        ::memcpy(base, record.constData() + first, recLen - first);
    }
    writePos() = (wPos + recLen) % bufSize;

    m_mutex.release();
    return true;
}

QByteArray MatrixRingBuffer::read()
{
    QByteArray result;

    m_mutex.acquire();

    int bufSize = dataSize();
    int rPos = readPos();
    int wPos = writePos();

    if (rPos == wPos) {
        m_mutex.release();
        return result;   // 无数据
    }

    char* base = static_cast<char*>(m_sharedMem.data()) + m_dataOffset;

    // 为了简化，我们假设记录总是从帧头开始，且读指针总是指向记录起始。
    // 我们将整个环形数据复制到线性缓冲区，再解析第一条记录。
    // （高效实现可分段读取，但为清晰起见，此处采用复制方式）
    int avail = (wPos - rPos + bufSize) % bufSize;
    QByteArray whole(avail, Qt::Uninitialized);
    if (rPos < wPos) {
        ::memcpy(whole.data(), base + rPos, avail);
    } else {
        int first = bufSize - rPos;
        ::memcpy(whole.data(), base + rPos, first);
        ::memcpy(whole.data() + first, base, wPos);
    }

    // 解析帧头、m、n，计算记录总长
    if (avail < 12) {   // 至少需要帧头 4 + m 4 + n 4
        // 数据不完整，跳过
        readPos() = wPos;   // 丢弃这些无效数据
        m_mutex.release();
        return result;
    }

    QDataStream stream(whole);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 header;
    int m, n;
    stream >> header;
    if (header != 0xAA55AA55) {
        // 帧头不匹配，可能是未对齐，跳到下一个可能帧头？这里简单丢弃所有数据
        readPos() = wPos;
        m_mutex.release();
        return result;
    }
    stream >> m >> n;
    int dataBytes = 2 * m * n * static_cast<int>(sizeof(float));
    int totalLen = 12 + dataBytes;   // 帧头4 + m4 + n4 + 数据

    if (totalLen > avail) {
        // 数据不完整（可能被覆盖），丢弃
        readPos() = wPos;
        m_mutex.release();
        return result;
    }

    result = whole.left(totalLen);
    readPos() = (rPos + totalLen) % bufSize;

    m_mutex.release();
    return result;
}

void MatrixRingBuffer::clear()
{
    m_mutex.acquire();
    writePos() = 0;
    readPos() = 0;
    m_mutex.release();
}
