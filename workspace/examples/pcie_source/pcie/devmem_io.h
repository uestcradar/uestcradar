#ifndef PCIE_SOURCE_DEVMEM_IO_H
#define PCIE_SOURCE_DEVMEM_IO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void *pcie_map(int fd, uint64_t address, size_t length, int writable);
void pcie_bar_write(void *bar, size_t offset, uint64_t value);
uint64_t pcie_bar_read(const void *bar, size_t offset);
#ifdef __cplusplus
}
#endif
#endif
