#ifndef _FAST_FLASH_H_
#define _FAST_FLASH_H_

// VX4B toolchain detection (was provided by lib_tflite_micro api.h)
#if defined(__riscv_xxcore) && !defined(__VX4B__)
#define __VX4B__ 1
#endif

#ifdef __XC__
#define chanend_t chanend
#else
#include <xcore/chanend.h>
#endif

#ifdef __XS3A__
// XS3: types and fl_connectToDevice() come from the tools' libquadflash.
#include <quadflash.h>
#elif defined(__VX4B__)
// VX4: same names, implemented in fast_flash_vx4.c on top of the tools'
// libflash (xmos_flash.h) and libflashdata (xmos_flashdata.h).

/** QSPI ports and clock block connected to the flash (same as on XS3). */
typedef struct {
  unsigned qspiCS;
  unsigned qspiSCLK;
  unsigned qspiSIO;
  unsigned qspiClkblk;
} fl_QSPIPorts;

/** Flash device description for VX4. */
typedef struct {
  unsigned num_pages;       ///< Number of 256-byte pages; 0 = discover via SFDP.
  unsigned core_clock_hz;   ///< xcore clock frequency.
  unsigned flash_sclk_hz;   ///< Maximum QSPI SCLK for the board/device.
} fl_QuadDeviceSpec;

/** Default for the xcore-400 EVK: discover size, 600 MHz core, 50 MHz SCLK. */
#define FL_QUADDEVICE_VX4_DEFAULT {0, 600000000, 50000000}

/** Connect to the flash and mount its data partition (uses spec[0]).
 * \returns 0 on success, negative on failure. */
int fl_connectToDevice(fl_QSPIPorts *qspi, const fl_QuadDeviceSpec spec[], unsigned n);

#else
#error "lib_flash_support: unsupported architecture (needs __XS3A__ or __VX4B__)"
#endif

/** Fast flash library.
 * Before calling any of the functions in here, lib_quad_flash must be initialised as normal by using
 * fl_connectToDevice(qspi, flash_spec, n_flash_spec).
 * After that, a call to fast_flash_init shall be made.
 * After that, a sequence of calls to fast_flash_read can be made.
 *
 * The data partition must start with the following 32 bytes: **NOTE: REMOVE THE +4 in fast_flash_init**
 *
 *   0xff, 0x00, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
 *   0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
 *   0x31, 0xf7, 0xce, 0x08, 0x31, 0xf7, 0xce, 0x08,
 *   0x9c, 0x63, 0x9c, 0x63, 0x9c, 0x63, 0x9c, 0x63
 * 
 * This pattern is designed to create maximum difficulties electrically and is used
 * to calibrate the electrical settings. Note that this pattern must be nibble reversed
 * before being written to flash; just like all other data.
 * The rest of the data partition can be used as normal
 */

/** The weights image written by xformer's generate_flash() starts with the
 * 32-byte training pattern, followed by this version word at offset 32. */
#define FAST_FLASH_IMAGE_VERSION_OFFSET 32
#define FAST_FLASH_IMAGE_VERSION ((1) | (2 << 8) | ((1 ^ 0xff) << 16) | ((2 ^ 0xff) << 24))

/** fast_flash_init() result when no weights image is found in flash. */
#define FAST_FLASH_ERROR_NO_IMAGE (-100)

#ifndef __XC__
/** True if w is the image version word, as stored or nibble-swapped. */
static inline int fast_flash_is_image_version(unsigned w) {
  unsigned swapped = ((w & 0x0f0f0f0fu) << 4) | ((w >> 4) & 0x0f0f0f0fu);
  return w == FAST_FLASH_IMAGE_VERSION || swapped == FAST_FLASH_IMAGE_VERSION;
}
#endif

/** Function that initialises the fast_flash library
 *
 * \param      qspi        ports that connect to flash
 *
 * Finds the weights image: at the data partition base reported by the tools
 * if the image is there, otherwise at address 0 (where "xflash --data" puts
 * it when no boot partition size is given).
 *
 * \returns    a negative value of -1..-5 if the window is too small (size 0..4)
 *             FAST_FLASH_ERROR_NO_IMAGE if no weights image is found
 *             zero if successful
 */
int fast_flash_init(fl_QSPIPorts *qspi);

/** Function that reads a sequential set of bytes from memory.
 * This function assumes that nibbles have been reversed ((x << 4) & 0xf0 | (x >> 4) & 0x0f)
 * before the data was written to flash.
 * Note that reading 32 bytes from offset 0 shall yield the special pattern above.
 *
 * \param      qspi        ports that connect to flash
 * \param      addr        address in flash data segment
 * \param      word_count  Number of words to read
 * \param      read_data   array to store data in to.
 * \param      c_out_data  optional channel end over which data is out() instead.
 */
void fast_flash_read(fl_QSPIPorts *qspi, unsigned addr, unsigned word_count, unsigned read_data[], chanend_t c_data_out);

#endif
