#ifndef _flash_server_h_
#define _flash_server_h_

#include "fast_flash.h"

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

#include "weights_server.h"   // from lib_tflite_micro (xmos_ai_tools runtime)

/** Flash names for the generic weights-server protocol. */
typedef weights_server_header_t flash_t;
typedef weights_server_cmd_t    flash_command_t;
#define FLASH_READ_PARAMETERS       WEIGHTS_READ_PARAMETERS
#define FLASH_READ_PARAMETERS_ASYNC WEIGHTS_READ_PARAMETERS_ASYNC
#define FLASH_READ_SYNCHRONIZE      WEIGHTS_READ_SYNCHRONIZE
#define FLASH_READ_XIP              WEIGHTS_READ_XIP
#define FLASH_SERVER_QUIT           WEIGHTS_SERVER_QUIT
#define FLASH_SERVER_INIT           WEIGHTS_SERVER_INIT

/**
 * Function that runs a flash-server. A flash server is a thread that serves one
 * or more clients. There is one flash server per flash-device, and the server
 * can serve clients on one or more tiles.
 *
 * The flash server takes the following commands:
 *   - Read a whole model from the flash.
 *   - Read some parameters from the flash
 *   - (future extension) Read code for an operator from flash
 *
 * This function does, at present, never return. It could be made to return if
 * all clients close their connection
 *
 * \param c_flash_clients Array of channels; one per client.
 *                        Each client is served in turn
 * \param headers         Space to store a header for each client
 *                        The header for the client describes the local
 * "filesystem" for that client \param n_flash_clients Number of clients. The
 * arrays in the first and second parameters should have this many entries
 * \param qspi            Structure holding the quad-flash ports. This contains
 * three Ports and a clock-block, the CS_N port, the CLK port, the DATA port and
 * a clock block to be used for the flash. \param flash_spec      Array holding
 * specificiations of flash devices, as per the libquadflash documentation
 * \param n_flash_spec    Number of elements in the spec array.
 */
#ifdef __XC__
void flash_server(chanend c_flash_clients[], flash_t headers[],
                  int n_flash_clients, fl_QSPIPorts &qspi,
                  fl_QuadDeviceSpec flash_spec[], int n_flash_spec);
#else
EXTERN_C void flash_server(chanend_t *c_flash_clients, flash_t *headers,
                  int n_flash_clients, fl_QSPIPorts *qspi,
                  fl_QuadDeviceSpec *flash_spec, int n_flash_spec);
#endif

#endif
