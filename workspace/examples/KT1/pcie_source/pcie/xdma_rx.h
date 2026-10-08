#ifndef PCIE_SOURCE_XDMA_RX_H
#define PCIE_SOURCE_XDMA_RX_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PCIE_RX_BASE UINT64_C(0x2080000000)
#define PCIE_BAR_BASE UINT64_C(0xef000000)
#define PCIE_PIPE_BYTES (192U * 1024U * 1024U)
#define PCIE_RX_BYTES (1536U * 1024U * 1024U)
#define PCIE_CONFIG_WORDS 3200

typedef struct {
    uint32_t offset, bytes, mask, group, control;
} PcieDescriptor;

typedef struct {
    uint64_t words[PCIE_CONFIG_WORDS];
    size_t count;
} PcieConfig;

typedef struct {
    int fd;
    void *bar;
    void *rx;
    size_t bar_length;
    int synchronized;
} PcieRx;

/* 1 valid descriptor, 0 empty sentinel, -1 unsupported/invalid descriptor. */
int pcie_parse_descriptor(uint64_t word, PcieDescriptor *descriptor);
int pcie_read_config(const char *path, PcieConfig *config);
/* Caller must verify platform/address ownership before calling open. */
int pcie_rx_open(PcieRx *rx, const PcieConfig *sync, const PcieConfig *drp);
int pcie_rx_poll(PcieRx *rx, PcieDescriptor *descriptor);
void pcie_rx_close(PcieRx *rx);
#ifdef __cplusplus
}
#endif
#endif
