#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstdint>
#include <stdexcept>

namespace my_block_data = cycore::algorithm::my_block;

class MyAlgorithm {
public:
    explicit MyAlgorithm(const cycore::sdk::Params& params)
        : factor_(params.get<double>("factor", 1.0)) {
        if (factor_ < -1.0e9 || factor_ > 1.0e9) {
            throw std::invalid_argument("factor is out of supported range");
        }
    }

    bool work(cycore::sdk::Reader<my_block_data::InputSample>& in,
              cycore::sdk::Writer<my_block_data::OutputSample>& out) {
        auto input = in.read_matrix(my_block_data::kInputRows, my_block_data::kInputCols);
        if (!input) {
            return false;
        }

        auto output = out.reserve_matrix(my_block_data::kOutputRows, my_block_data::kOutputCols);
        if (!output) {
            return false;
        }

        for (std::size_t row = 0; row < input->rows(); ++row) {
            for (std::size_t col = 0; col < input->cols(); ++col) {
                (*output)(row, col) = (*input)(row, col) * static_cast<my_block_data::OutputSample>(factor_);
            }
        }
        return true;
    }

private:
    double factor_ = 1.0;
};

CYCORE_EXPORT_ALGORITHM(
    "my_plugin",
    "algorithm.my_block",
    MyAlgorithm,
    cycore::algorithm::my_block::InputSample,
    cycore::algorithm::my_block::OutputSample
)
