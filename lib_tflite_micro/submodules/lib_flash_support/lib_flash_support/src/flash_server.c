// Weights server over QSPI flash; architecture-independent.
// The flash access backend is fast_flash_xs3.c or fast_flash_vx4.c (see fast_flash.h).

#include <stdio.h>
#include <print.h>
#include <stdint.h>
#include <platform.h>
#include <assert.h>
#include <xcore/channel.h>
#include <xcore/select.h>
#include "flash_server.h"

#include "fast_flash.h"

#define TMP_BUF_SIZE_IN_BYTES  1024


static int flash_version_check(fl_QSPIPorts *qspi) {
    uint32_t tmp[2];
    fast_flash_read(qspi, /*unsigned addr*/FAST_FLASH_IMAGE_VERSION_OFFSET, /*unsigned word_count*/1, /*unsigned read_data[]*/(unsigned *)tmp, /*chanend ?c_data_out*/ 0);

    return tmp[0] ^ FAST_FLASH_IMAGE_VERSION;
}



static int flash_server_operate(chanend_t c_flash, flash_t *headers, fl_QSPIPorts *qspi) {
    int address, bytes;
    flash_command_t cmd;
    cmd = chan_in_word(c_flash);
    if (cmd == FLASH_READ_PARAMETERS) {
        // Set not parallel mode
        chan_out_word(c_flash, 0);
        address = chan_in_word(c_flash);
        bytes   = chan_in_word(c_flash);
        address = headers->parameters_start + address;
        fast_flash_read(qspi, address, bytes/4, /*not using this arg*/NULL, c_flash);
    } else if (cmd == FLASH_READ_PARAMETERS_ASYNC) {
        int N;
        int target_address[WEIGHTS_SERVER_MAX_BLOCKS];
        int size_in_bytes[WEIGHTS_SERVER_MAX_BLOCKS];
        address = chan_in_word(c_flash);
        N       = chan_in_word(c_flash);
        assert(N <= WEIGHTS_SERVER_MAX_BLOCKS);
        for(int i = 0; i < N; i++) {
            size_in_bytes[i] = chan_in_word(c_flash);
            target_address[i] = chan_in_word(c_flash);
        }
        address = headers->parameters_start + address;
        for(int i = 0; i < N; i++) {
            fast_flash_read(qspi, address, size_in_bytes[i]/4, (unsigned *)(target_address[i])
                , /*When zero, data is directly written into target address*/0);
            address += size_in_bytes[i];
        }
        chanend_out_end_token(c_flash);
    } else if (cmd == FLASH_SERVER_INIT) {
        ; // TODO
    } else if (cmd == FLASH_SERVER_QUIT) {
        return 0;
    }
    return 1;
}


void flash_server(chanend_t c_flash[], flash_t headers[], int n_flash,
                  fl_QSPIPorts *qspi, fl_QuadDeviceSpec flash_spec[],
                  int n_flash_spec) {
    int res;
    assert((res = fl_connectToDevice(qspi, flash_spec, n_flash_spec)) == 0);    
    assert((res = fast_flash_init(qspi)) == 0);
    assert((res = flash_version_check(qspi)) == 0);
    
    fast_flash_read(qspi, /*unsigned addr*/36, /*unsigned word_count*/(n_flash * sizeof(flash_t))/4, /*unsigned read_data[]*/(unsigned*)headers, /*chanend ?c_data_out*/ 0);
    
    if (n_flash == 1) {
        while(flash_server_operate(c_flash[0], &headers[0], qspi)) {
            ;
        }
    } else if (n_flash == 2) {  // This is a bit unpleasant
        SELECT_RES(
            CASE_THEN(c_flash[0], channel0),
            CASE_THEN(c_flash[1], channel1)
            )
        {
        channel0:
            if (flash_server_operate(c_flash[0], &headers[0], qspi)) {
                SELECT_CONTINUE_NO_RESET;
            } else {
                break;
            }
        channel1:
            if (flash_server_operate(c_flash[1], &headers[1], qspi)) {
                SELECT_CONTINUE_NO_RESET;
            } else {
                break;
            }
        }
    } else {
        assert(n_flash);
    }
}

