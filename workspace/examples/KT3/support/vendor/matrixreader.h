#ifndef MATRIXREADER_H
#define MATRIXREADER_H

#include <vector>
#include <complex>

/**
 * @brief 外部读取矩阵数据的接口（静态方法）
 */
class MatrixReader
{
public:
    /**
     * @brief 获取共享内存中最新的矩阵数据（读后该记录被移除）
     * @param m 输出行数
     * @param n 输出列数
     * @param A 输出矩阵（列优先存储，大小 m*n）
     * @return 成功读取返回 true，无数据或解析失败返回 false
     */
    static bool getLatestMatrix(int& m, int& n, std::vector<double>& A);
};

#endif // MATRIXREADER_H
