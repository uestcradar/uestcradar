#include "matrixreader.h"
#include "matrixringbuffer.h"
#include <QDataStream>

bool MatrixReader::getLatestMatrix(int& m, int& n, std::vector<double>& A)
{
    MatrixRingBuffer buffer("MatrixBuffer", 32 * 1024 * 1024);
    QByteArray record = buffer.read();
    if (record.isEmpty())
        return false;

    QDataStream stream(record);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 header;
    stream >> header;
    if (header != 0xAA55AA55)
        return false;

    stream >> m >> n;
    int total = m * n;
    A.resize(total);

    // 按列优先，直接读取 double 单值（8B/元素）
    for (int col = 0; col < n; ++col) {
        for (int row = 0; row < m; ++row) {
            stream >> A[row * n + col];
        }
    }
    return true;
}
