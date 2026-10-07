#include <data.h>

#include <utility>

namespace {

using namespace uestcradar;

// This function is intentionally not executed. Compiling and linking it proves
// that every current frame can be a trace parent without adding pairwise SDK
// overloads or exported symbols.
void compile_all_linked_create_combinations(
    Output<IQFrame>& iq_output,
    Output<PulseCompressionFrame>& pulse_output,
    Output<RDFrame>& rd_output,
    const IQMetadata& iq_metadata,
    const PulseCompressionMetadata& pulse_metadata,
    const RDMetadata& rd_metadata,
    const IQFrame& iq,
    const PulseCompressionFrame& pulse,
    const RDFrame& rd) {
    static_cast<void>(iq_output.create(iq_metadata, iq));
    static_cast<void>(iq_output.create(iq_metadata, pulse));
    static_cast<void>(iq_output.create(iq_metadata, rd));
    static_cast<void>(pulse_output.create(pulse_metadata, iq));
    static_cast<void>(pulse_output.create(pulse_metadata, pulse));
    static_cast<void>(pulse_output.create(pulse_metadata, rd));
    static_cast<void>(rd_output.create(rd_metadata, iq));
    static_cast<void>(rd_output.create(rd_metadata, pulse));
    static_cast<void>(rd_output.create(rd_metadata, rd));
}

}  // namespace

volatile auto linked_create_compile_check =
    &compile_all_linked_create_combinations;

int main() {
    return linked_create_compile_check == nullptr ? 1 : 0;
}
