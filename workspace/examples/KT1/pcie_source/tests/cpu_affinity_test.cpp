#include "cpu_affinity.hpp"
#include "dma_snapshot.hpp"
#include <cassert>
#include <vector>

int main() {
    using namespace pcie_source;
    for (auto bad : {"", "8,9", "8,9,10,11", "8,8,10", "8,9,9", "-1,9,10", "8,,10", "8,9,1024", "8,9,10x"}) {
        try { (void)parse_cpu_map(bad); assert(false); } catch (const std::invalid_argument&) {}
    }
    assert((parse_cpu_map("8,9,10") == CpuMap{8,9,10}));
    cpu_set_t original;
    assert(pthread_getaffinity_np(pthread_self(), sizeof(original), &original) == 0);
    std::vector<int> available;
    for (int i=0; i<CPU_SETSIZE; ++i) if (CPU_ISSET(i, &original)) available.push_back(i);
    assert(!available.empty());
    try { validate_cpu_map({available[0],available[0],available[0]}, false); assert(false); }
    catch (const std::invalid_argument&) {}
    for (int i=0; i<100; ++i) {
        try { DmaSnapshot copy(CPU_SETSIZE); assert(false); } catch (const std::invalid_argument&) {}
    } // Binding failure must join the already-created helper, not terminate or hang.
    std::thread thread([&] {
        bind_cpu(pthread_self(), available[0]);
        assert(sched_getcpu() == available[0]);
        if (available.size() > 1) {
            // A child inherits the narrow RX mask but may bind another originally allowed CPU.
            std::thread child([&] {
                bind_cpu(pthread_self(), available[1]);
                assert(sched_getcpu() == available[1]);
            });
            child.join();
            try { validate_cpu_map({available[0],available[1],0}, true); assert(false); }
            catch (const std::invalid_argument&) {}
            DmaSnapshot copy(available[1]);
            std::vector<std::byte> data(32768, std::byte{42}), a(data.size()), b(data.size());
            copy.copy(a,b,data);
            assert(a==data && b==data);
        }
    });
    thread.join();
    cpu_set_t after;
    assert(pthread_getaffinity_np(pthread_self(), sizeof(after), &after) == 0);
    assert(CPU_EQUAL(&after, &original));
}
