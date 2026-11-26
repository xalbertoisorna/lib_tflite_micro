// VX4 flash backend: implements fl_connectToDevice / fast_flash_init /
// fast_flash_read (see fast_flash.h) on top of the tools' libflash
// (xmos_flash.h) and libflashdata (xmos_flashdata.h).
// Link with: -lflash -lflashdata
#include "fast_flash.h"   // defines __VX4B__ from __riscv_xxcore if needed

#if defined(__VX4B__)

#include <stdio.h>
#include <stdbool.h>
#include <xcore/chanend.h>
#include <xmos_flash.h>
#include <xmos_flashdata.h>

#define VX4_REFERENCE_CLOCK_HZ 100000000
#define READ_CHUNK_WORDS       64

// Note: unlike the XS3 bit-banged reader, libflash's reads already return the
// FlashBuilder image in the right byte/nibble order (verified on XK-EVK-XU416).

static flash_ports_t       g_ports;   // must be a global (libflash requirement)
static flash_handle_t      g_flash;
static flashdata_handle_t  g_flashdata;
static flashboost_handle_t g_boost;
static fl_QuadDeviceSpec   g_spec;
static unsigned            g_data_offset;
static unsigned            g_device_bytes;
static int                 g_reported_base;   // from the partition table, or -1
static int                 g_boost_active;

int fl_connectToDevice(fl_QSPIPorts *qspi, const fl_QuadDeviceSpec spec[], unsigned n) {
    if (n < 1) return -1;
    g_spec = spec[0];

    g_ports.flash_cs      = qspi->qspiCS;
    g_ports.flash_sclk    = qspi->qspiSCLK;
    g_ports.flash_sio     = qspi->qspiSIO;
    g_ports.flash_clk_blk = qspi->qspiClkblk;
    g_ports.flash_mosi    = 0;   // QSPI only
    g_ports.flash_miso    = 0;

    flash_clock_setup_t setup = {g_spec.flash_sclk_hz, g_spec.core_clock_hz, VX4_REFERENCE_CLOCK_HZ};
    flash_clock_config_t clock_config;
    if (flash_clock_make_config(&clock_config, &setup) != 0) return -2;
    flash_connect(&g_flash, &g_ports, clock_config);

    unsigned num_pages = g_spec.num_pages ? g_spec.num_pages : flash_discover_num_pages(&g_flash);
    if (num_pages == 0) return -3;
    g_device_bytes = num_pages * 256;

    // The partition table only exists if a program image was flashed;
    // fast_flash_init() checks the result and falls back to 0.
    g_reported_base = -1;
    if (flashdata_mount(&g_flashdata, &g_flash, num_pages) == flashdata_result_success) {
        g_reported_base = flashptn_data_address(&g_flashdata);
    }
    return 0;
}

static unsigned swap_nibbles(unsigned w) {
    return ((w & 0x0f0f0f0fu) << 4) | ((w >> 4) & 0x0f0f0f0fu);
}

// The image is stored nibble-swapped in flash (generate_flash() does this for
// the XS3 bit-banged reader). flash_read_boost() undoes that in the transfer,
// like the XS3 reader; flash_read_quad() returns the bytes as stored, so its
// data has to be swapped back. (Verified on XK-EVK-XU416.)
static void read_quad(unsigned byte_address, unsigned *dst, unsigned num_words) {
    flash_read_quad(&g_flash, byte_address, dst, num_words);
    for (unsigned i = 0; i < num_words; i++) dst[i] = swap_nibbles(dst[i]);
}

// True if the weights image starts at absolute flash address base.
static int image_at(int base) {
    if (base < 0 || (unsigned)base + FAST_FLASH_IMAGE_VERSION_OFFSET + 4 > g_device_bytes) return 0;
    unsigned w;
    read_quad((unsigned)base + FAST_FLASH_IMAGE_VERSION_OFFSET, &w, 1);
    return w == FAST_FLASH_IMAGE_VERSION;
}

// Calibrate and enter boost mode using the pattern at pattern_address.
static int start_boost(unsigned pattern_address, unsigned pattern_version) {
    flash_clock_setup_t setup = {g_spec.flash_sclk_hz, g_spec.core_clock_hz, VX4_REFERENCE_CLOCK_HZ};
    flashboost_clock_config_t boost_config;
    if (flashboost_clock_make_config(&boost_config, &g_flash, &setup,
                                     pattern_address, pattern_version, true) != flashboost_success) {
        return 0;
    }
    if (flashboost_start(&g_boost, &g_flash, &boost_config) != flashboost_success) {
        return 0;
    }
    g_boost_active = 1;
    return 1;
}

int fast_flash_init(fl_QSPIPorts *qspi) {
    (void)qspi;
    if (image_at(g_reported_base)) {
        g_data_offset = (unsigned)g_reported_base;
    } else if (image_at(0)) {
        g_data_offset = 0;
        printf("Weights image not at reported data partition base %d; using 0\n", g_reported_base);
    } else {
        printf("No weights image found (tried %d and 0)\n", g_reported_base);
        return FAST_FLASH_ERROR_NO_IMAGE;
    }
    printf("Data partition base: %u\n", g_data_offset);

    // Calibrate fast "boost" reads. First against the bootloader's pattern
    // (only when the partition table is valid, i.e. the image was where it
    // said), then against the 32-byte pattern at the start of our own image.
    // If neither works, use ordinary quad reads.
    g_boost_active = 0;
    const char *pattern_used = "none";
    int partition_table_valid = (g_data_offset == (unsigned)g_reported_base);
    if (partition_table_valid) {
        int address = flashdata_boost_address(&g_flashdata);
        int version = flashdata_boost_version(&g_flashdata);
        if (address >= 0 && version >= 0 && start_boost((unsigned)address, (unsigned)version)) {
            pattern_used = "bootloader";
        }
    }
    if (!g_boost_active && start_boost(g_data_offset, 1)) {
        pattern_used = "image";
    }
    printf("Flash reads: %s (calibration pattern: %s)\n", g_boost_active ? "boost" : "quad", pattern_used);
    return 0;
}

static void read_words(unsigned byte_address, unsigned *dst, unsigned num_words) {
    if (g_boost_active) {
        // While boost is active only boost reads may use the device.
        if (flash_read_boost(&g_boost, byte_address, dst, num_words) != flashboost_success) {
            // Boost failed: leave boost mode and use ordinary reads from now on.
            flashboost_finish(&g_boost);
            g_boost_active = 0;
        }
    }
    if (!g_boost_active) {
        read_quad(byte_address, dst, num_words);
    }
}

void fast_flash_read(fl_QSPIPorts *qspi, unsigned addr, unsigned word_count,
                     unsigned read_data[], chanend_t c_data_out) {
    (void)qspi;
    unsigned byte_address = g_data_offset + addr;

    if (!c_data_out) {
        read_words(byte_address, read_data, word_count);
        return;
    }

    // Stream to the client exactly like the XS3 reader: word_count words
    // with no handshake, then an END token (see load_weights_synchronous).
    unsigned buf[READ_CHUNK_WORDS];
    while (word_count) {
        unsigned n = word_count < READ_CHUNK_WORDS ? word_count : READ_CHUNK_WORDS;
        read_words(byte_address, buf, n);
        for (unsigned i = 0; i < n; i++) {
            chanend_out_word(c_data_out, buf[i]);
        }
        byte_address += 4 * n;
        word_count   -= n;
    }
    chanend_out_end_token(c_data_out);
}

#endif // defined(__VX4B__)
