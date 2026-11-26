#pragma once

/**
 * Generic wire protocol between the weights client (load_weights.c, used by the
 * XC_ld_weights kernel) and a weights server. The client does not know which
 * server is on the other end of its chanend; any server implementing this
 * protocol can serve it:
 *
 *   - tile_ram_server (lib_tflite_micro): weights in another tile's RAM
 *                                         header type: tile_ram_header_t
 *   - flash_server    (lib_flash_support): weights in QSPI flash
 *                                         header type: flash_t (+ FLASH_* names)
 *
 * Definitions only; no code. The header layout is produced by xformer
 * (FlashBuilder in xformer/flash.py, Utils/TileRamSupport.cpp).
 */

/** Maximum number of blocks in one WEIGHTS_READ_PARAMETERS_ASYNC request. */
#define WEIGHTS_SERVER_MAX_BLOCKS 2

/** Per-client "file system" header, describing where each section of a
 * client's data starts in the server's memory (flash, tile RAM, ...).
 *
 * The main program must allocate one per client before starting the server;
 * the server fills it in from its memory.
 */
typedef struct weights_server_header {
  int model_start;            ///< Start address for model.
  int parameters_start;       ///< Start address of parameters.
  int operators_start;        ///< Start address for operator-binaries.
  int execute_in_place_start; ///< Start address for execute-in-place code.
} weights_server_header_t;

/** Commands a weights server accepts. */
typedef enum weights_server_cmd {
  WEIGHTS_READ_PARAMETERS       = 0, ///< Read a set of parameters.
  WEIGHTS_READ_PARAMETERS_ASYNC = 1, ///< Read parameters asynchronously.
  WEIGHTS_READ_SYNCHRONIZE      = 2, ///< Complete async read.
  WEIGHTS_READ_XIP              = 3, ///< Read code to execute-in-place (future extension).
  WEIGHTS_SERVER_QUIT           = 4, ///< Stop the server.
  WEIGHTS_SERVER_INIT           = 5, ///< Initialise the server (e.g. flash timing setup).
  //WEIGHTS_READ_PARAMETERS_COMPRESSED_FLOAT = 6,
} weights_server_cmd_t;
