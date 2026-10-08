/* Adapted from cycore/lib/du/resource/device/pcie/src/devmem_io.c.
 * Retains physical MMIO behavior; ownership/cleanup belongs to xdma_rx.c. */
#define _FILE_OFFSET_BITS 64
#include "devmem_io.h"
#include <sys/mman.h>

void *pcie_map(int fd, uint64_t address, size_t length, int writable)
{
    return mmap(NULL, length, PROT_READ | (writable ? PROT_WRITE : 0),
                MAP_SHARED, fd, (off_t)address);
}

void pcie_bar_write(void *bar, size_t offset, uint64_t value)
{
    __sync_synchronize();
    *(volatile uint64_t *)((unsigned char *)bar + offset) = value;
    __sync_synchronize();
}

uint64_t pcie_bar_read(const void *bar, size_t offset)
{
    __sync_synchronize();
    const uint64_t value = *(const volatile uint64_t *)((const unsigned char *)bar + offset);
    __sync_synchronize();
    return value;
}
