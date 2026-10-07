/* Receive-only adaptation of cycore/lib/du/resource/device/pcie/src/xdma_fun.c.
 * Original register addresses, descriptor layout and SYNC_1_4/DRP_4G8 write order.
 * No TX path, pointer queue, inferred hardware ownership or firmware reset. */
#define _DEFAULT_SOURCE
#define _FILE_OFFSET_BITS 64
#include "xdma_rx.h"
#include "devmem_io.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <unistd.h>

int pcie_parse_descriptor(uint64_t word, PcieDescriptor *d)
{
    if (!d) { errno = EINVAL; return -1; }
    memset(d, 0, sizeof(*d));
    if (word == UINT64_MAX) return 0;
    d->mask = (word >> 56) & 0xff;
    d->group = (word >> 52) & 0xf;
    d->control = ((word >> 50) & 3) == 1;
    d->offset = (uint32_t)(((word >> 28) & 0xfffff) << 11);
    d->bytes = word & 0xfffffff;
    if (d->group == 15) return 1; /* BIT: count only; never dereference as IQ. */
    if (d->group != 0 || (d->mask & 15) != 15 || d->bytes == 0 ||
        d->offset >= PCIE_PIPE_BYTES || d->bytes > PCIE_PIPE_BYTES - d->offset ||
        (!d->control && d->bytes % 8 != 0)) {
        errno = EPROTO;
        return -1;
    }
    return 1;
}

int pcie_read_config(const char *path, PcieConfig *config)
{
    if (!path || !config) { errno = EINVAL; return -1; }
    memset(config, 0, sizeof(*config));
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char line[64];
    while (fgets(line, sizeof(line), file)) {
        const size_t length = strcspn(line, "\r\n");
        if (length != 16 || config->count == PCIE_CONFIG_WORDS) goto invalid;
        for (size_t j = length; line[j]; ++j)
            if (line[j] != '\r' && line[j] != '\n') goto invalid;
        uint64_t value = 0;
        for (size_t i = 0; i < length; ++i) {
            const unsigned char c = (unsigned char)line[i];
            if (!isxdigit(c)) goto invalid;
            value = (value << 4) | (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
        }
        config->words[config->count++] = value;
    }
    if (ferror(file) || config->count == 0) goto invalid;
    fclose(file);
    return 0;
invalid:
    fclose(file);
    config->count = 0;
    errno = EINVAL;
    return -1;
}

void pcie_rx_close(PcieRx *rx)
{
    if (!rx) return;
    if (rx->rx && rx->rx != MAP_FAILED) munmap(rx->rx, PCIE_RX_BYTES);
    if (rx->bar && rx->bar != MAP_FAILED) munmap(rx->bar, rx->bar_length);
    if (rx->fd >= 0) close(rx->fd);
    memset(rx, 0, sizeof(*rx));
    rx->fd = -1;
}

int pcie_rx_open(PcieRx *rx, const PcieConfig *sync, const PcieConfig *drp)
{
    if (!rx || !sync || !drp || !sync->count || !drp->count ||
        sync->count > PCIE_CONFIG_WORDS || drp->count > PCIE_CONFIG_WORDS) {
        errno = EINVAL; return -1;
    }
    memset(rx, 0, sizeof(*rx));
    rx->fd = -1;
    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || PCIE_BAR_BASE % (uint64_t)page || PCIE_RX_BASE % (uint64_t)page) {
        errno = EINVAL; return -1;
    }
    rx->bar_length = (size_t)page;
    rx->fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (rx->fd < 0) return -1;
    /* Cooperative exclusion for this receiver; preflight must check other users. */
    if (flock(rx->fd, LOCK_EX | LOCK_NB) != 0) goto fail;
    rx->bar = pcie_map(rx->fd, PCIE_BAR_BASE, rx->bar_length, 1);
    if (rx->bar == MAP_FAILED) goto fail;
    rx->rx = pcie_map(rx->fd, PCIE_RX_BASE, PCIE_RX_BYTES, 0);
    if (rx->rx == MAP_FAILED) goto fail;
    /* Same sequence as bslFiberRecvCfgFileLoad(SYNC_1_4 | DRP_4G8). */
    pcie_bar_write(rx->bar, 0xc0, PCIE_RX_BASE);
    usleep(1000000);
    for (size_t i = 0; i < sync->count; ++i) pcie_bar_write(rx->bar, 0, sync->words[i]);
    usleep(2000);
    for (size_t i = 0; i < drp->count; ++i) pcie_bar_write(rx->bar, 0, drp->words[i]);
    usleep(2000);
    return 0;
fail: {
    const int error = errno;
    pcie_rx_close(rx);
    errno = error;
    return -1;
}
}

int pcie_rx_poll(PcieRx *rx, PcieDescriptor *descriptor)
{
    if (!rx || !rx->bar || rx->bar == MAP_FAILED) { errno = EINVAL; return -1; }
    const uint64_t word = pcie_bar_read(rx->bar, 0);
    if (!rx->synchronized) {
        /* Old implementation discards stale FIFO entries until the empty sentinel. */
        if (word == UINT64_MAX) rx->synchronized = 1;
        return 0;
    }
    return pcie_parse_descriptor(word, descriptor);
}
