#ifndef MATRIXRINGBUFFER_H
#define MATRIXRINGBUFFER_H

#include <QSharedMemory>
#include <QSystemSemaphore>
#include <QByteArray>

/**
 * @brief 跨进程环形共享内存缓冲区，用于存储结构化记录（如矩阵数据）
 *
 * 每条记录以帧头开始，记录长度可变。当空间不足时自动覆盖最旧数据。
 * 使用 QSystemSemaphore 保证多进程互斥访问。
 */
class MatrixRingBuffer
{
public:
    /**
     * @param key 共享内存唯一标识（不同用途应使用不同 key）
     * @param size 缓冲区总大小（字节），包含头部开销
     */
    explicit MatrixRingBuffer(const QString& key, int size);
    ~MatrixRingBuffer();

    /**
     * @brief 写入一条完整记录
     * @param record 待写入的数据（必须包含帧头等信息）
     * @return 成功返回 true；若记录长度超过缓冲区容量则返回 false
     */
    bool write(const QByteArray& record);

    /**
     * @brief 读取一条完整记录（按写入顺序返回，读指针后移）
     * @return 若缓冲区为空或数据损坏，返回空 QByteArray；否则返回完整记录
     */
    QByteArray read();

    /**
     * @brief 清空缓冲区（重置读写指针）
     */
    void clear();

private:
    QSharedMemory     m_sharedMem;
    QSystemSemaphore  m_mutex;        // 跨进程互斥锁
    const int         m_dataOffset;   // 数据区起始偏移（固定 32 字节）

    // 以下方法需在 attach 后调用，且假定已加锁
    char*       dataPtr()       { return static_cast<char*>(m_sharedMem.data()); }
    quint32&    magic()         { return *reinterpret_cast<quint32*>(dataPtr()); }
    int&        writePos()      { return *reinterpret_cast<int*>(dataPtr() + 4); }
    int&        readPos()       { return *reinterpret_cast<int*>(dataPtr() + 8); }
    int&        dataSize()      { return *reinterpret_cast<int*>(dataPtr() + 12); }

    static const quint32 MAGIC_NUMBER = 0x5A5A5A5A;
};

#endif // MATRIXRINGBUFFER_H
