// XS3 flash backend: libquadflash + bit-banged fast reads (fast_flash_read_loop_xs3.S)
#if defined(__XS3A__)

#include <xs1.h>
#include <platform.h>
#include <timer.h>
#include <stdio.h>
#include <xclib.h>
#include <stdint.h>
#include <xcore/port.h>
#include <xcore/clock.h>

#include "fast_flash.h"
#include <quadflashlib.h>   // fl_setBootPartitionSize()

static int best_read_adj;
static int data_offset;

// Defines for SETPADCTRL

#define DR_STR_2mA     0
#define DR_STR_4mA     1
#define DR_STR_8mA     2
#define DR_STR_12mA    3

#define PORT_PAD_CTL_SMT    0           // Schmitt off
#define PORT_PAD_CTL_SR     1           // Fast slew
#define PORT_PAD_CTL_DR_STR DR_STR_8mA  // 8mA drive
#define PORT_PAD_CTL_REN    1           // Receiver enabled
#define PORT_PAD_CTL_MODE   0x0006

#define PORT_PAD_CTL ((PORT_PAD_CTL_SMT     << 23) | \
                      (PORT_PAD_CTL_SR      << 22) | \
                      (PORT_PAD_CTL_DR_STR  << 20) | \
                      (PORT_PAD_CTL_REN     << 17) | \
                      (PORT_PAD_CTL_MODE    << 0))
                      
#define HIGH_DRIVE_8MA

// Define the clock source divide - 600MHz/800MHz core clock divided by (2*CLK_DIVIDE)
// CORE CLOCK   600MHz    800MHz
// CLK_DIVIDE      SPI_CLK
// 3            100MHz    133MHz
// 4            75MHz     100MHz
// 5            60MHz     80MHz
// 6            50MHz     66MHz

extern unsigned int fl_getDataPartitionBase(void);

// Address in Flash where the training pattern starts
// How many 32 bit words are in the pattern
#define PATTERN_WORDS 8

extern void fast_read_loop(fl_QSPIPorts *qspi, unsigned addr, unsigned mode, unsigned read_adj, unsigned read_count, unsigned read_data[], chanend_t c_data_out);

static int clk_divider;

static void set_pad_delay(port_t p, int pad_delay) {
    int c_word = XS1_SETC_MODE_LONG;
    c_word = XS1_SETC_LMODE_SET(c_word, XS1_SETC_LMODE_PIN_DELAY); 
    c_word = XS1_SETC_VALUE_SET(c_word, pad_delay); 
    port_write_control_word(p, c_word); 
}

static void ports_clocks_setup(fl_QSPIPorts *qspi)
{
    unsigned int ref_clk_div;
    read_sswitch_reg(get_local_tile_id(), XS1_SSWITCH_REF_CLK_DIVIDER_NUM, &ref_clk_div);
    clk_divider = (ref_clk_div + 1)/2;
    if (clk_divider == 0) {
        // The reference clock divider register reads 0 under xsim (and a divider
        // of 0 would skip the whole timing sweep below). Fall back to 3, i.e.
        // 100MHz SPI from a 600MHz core.
        clk_divider = 3;
    }
    // Define a clock source - core clock divided by (2*clk_divider)

    clock_enable(qspi->qspiClkblk);
    clock_set_source_clk_xcore(qspi->qspiClkblk);
    clock_set_divide(qspi->qspiClkblk, clk_divider);

    // For experimentation only do not use.
    //set_clock_fall_delay(qspi->qspiClkblk, 1);
    
    // Configure this clock to be output on the qspi->qspiSCLK port
    port_enable(qspi->qspiSCLK);
    port_set_clock(qspi->qspiSCLK, qspi->qspiClkblk);
    port_set_out_clock(qspi->qspiSCLK);
    
    // Define all ports initially as being outputs clocked by the qspi->qspiClkblk clock. Initial state for csn is set to 1 (inactive).
    port_start_buffered(qspi->qspiSIO, 32);
    port_set_clock(qspi->qspiSIO, qspi->qspiClkblk);
    port_out(qspi->qspiSIO, 0xFFFFFFFF);

    port_enable(qspi->qspiCS);

    clock_start(qspi->qspiClkblk);
    // Set the drive strength to 8mA on all ports.
    // This is optional. If used it may change timing tuning so must tune with the settings we are using.
    // This will reduce rise time uncertainty and ensure ports will fully switch at 100MHz rate over PVT. (Remember our corner silicon was only skewed for core transistors not IO).
    // Setting all ports to the same drive to ensure we don't introduce any extra skew between outputs.
    // Downside is extra EMI.
#ifdef HIGH_DRIVE_8MA
    asm volatile ("setc res[%0], %1" :: "r" (qspi->qspiSCLK), "r" (PORT_PAD_CTL));
    asm volatile ("setc res[%0], %1" :: "r" (qspi->qspiSIO), "r" (PORT_PAD_CTL));
    asm volatile ("setc res[%0], %1" :: "r" (qspi->qspiCS), "r" (PORT_PAD_CTL));
#endif

}

static unsigned read_data_check[PATTERN_WORDS] = {
    0x0f0f00ff,
    0x0f0f0f0f,
    0x00ff00ff,
    0x00ff00ff,
    0x08cef731,
    0x08cef731,
    0x639c639c,
    0x639c639c
};

// True if the weights image starts at absolute flash address base.
// Uses libquadflash's ordinary (untuned) reads, relative to the boot partition size.
static int image_at(unsigned base) {
    if (base + FAST_FLASH_IMAGE_VERSION_OFFSET + 4 > fl_getFlashSize()) return 0;
    fl_setBootPartitionSize(base);
    unsigned char b[4];
    if (fl_readData(FAST_FLASH_IMAGE_VERSION_OFFSET, 4, b) != 0) return 0;
    return fast_flash_is_image_version(b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24));
}

int fast_flash_init(fl_QSPIPorts *qspi) {
    unsigned reported = fl_getDataPartitionBase();
    if (image_at(reported)) {
        data_offset = reported;
    } else if (image_at(0)) {
        data_offset = 0;
        printf("Weights image not at reported data partition base %u; using 0\n", reported);
    } else {
        printf("No weights image found (tried %u and 0)\n", reported);
        return FAST_FLASH_ERROR_NO_IMAGE;
    }
    printf("Data partition base: %d\n", data_offset);

    // Setup the clock block and ports
    ports_clocks_setup(qspi);

    unsigned read_data_tmp[PATTERN_WORDS];
    int passing_words;
    
    // Declare the results as an array of 36 (max) chars.
    // Bit 0 sdelay, bits 1-2 read adj, bits 3-5 pad delay, bit 7 is pass/fail.
    // the index into the array becomes the nominal time.
    char results[6*4];  // clk_divider is at most 4
    
    // So, lets run the testing and collect pass/fail results.
    // Keep an index of the time we are looping across.
    unsigned char time_index = 0;
    int pass_count = 0;
    int pass_start = 0;
    
    // This loops over the settings in such a way that the result is sequentially increasing in time in units of core clocks.
    for(int read_adj = 0; read_adj < 3; read_adj++) // Data read port time adjust
    {
        for(int sdelay = 0; sdelay < 2; sdelay++) // Sample delays
        {
            if (sdelay == 1) {
                port_set_sample_falling_edge(qspi->qspiSIO); // Set data port to sample on a falling edge instead of rising
            } else {
                port_set_sample_rising_edge(qspi->qspiSIO);
            }
            for(int pad_delay = (clk_divider - 1); pad_delay >= 0; pad_delay--) // Pad delays (only loop over useful pad delays)
            {
                // Set input pad delay in units of core clocks
                set_pad_delay(qspi->qspiSIO, pad_delay);

                // Read the data with the current settings
                fast_read_loop(qspi, data_offset, 0, read_adj, PATTERN_WORDS, read_data_tmp, 0);
                
                // Check if the data is correct
                passing_words = 0;
                for (int m = 0; m < PATTERN_WORDS; m++)
                {
                    if (read_data_tmp[m] == read_data_check[m]) {
                        passing_words++;
                    }
                }
                char setting = sdelay | (read_adj << 1) | (pad_delay << 3);
                // Store the settings and pass/fail in the results
                if (passing_words == PATTERN_WORDS) {
                    //printf("%d: OK\n", setting);
                    if (pass_count == 0) // This is first PASS we've seen
                    {
                        pass_start = time_index; // Record the setting index
                    }
                    pass_count++;
                    results[time_index] = setting | 1 << 7;
                } else {
                    //printf("%d: BAD\n", setting);
                    results[time_index] = setting;
                }
                time_index++;
            }
        }
    }
    
    char best_setting = pass_start + (pass_count >> 1); // Pick the middle setting
    
    if (pass_count < 5) {
        return -1-pass_count;
    }

    best_read_adj  = (results[best_setting] & 0x06) >> 1;
    if (results[best_setting] & 0x01) {   // Sdelay set?
        port_set_sample_falling_edge(qspi->qspiSIO); // Set data port to sample on a falling edge instead of rising
    } else {
        port_set_sample_rising_edge(qspi->qspiSIO);
    }
    set_pad_delay(qspi->qspiSIO, (results[best_setting] & 0x38) >> 3);
    return 0;
}


void fast_flash_read(fl_QSPIPorts *qspi, unsigned addr, unsigned read_count, unsigned read_data[], chanend_t c_data_out) {
    fast_read_loop(qspi, data_offset + addr, 0, best_read_adj, read_count, read_data, c_data_out);
}

#endif // defined(__XS3A__)
