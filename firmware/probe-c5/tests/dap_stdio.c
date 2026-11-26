#include "DAP_config.h"
#include "DAP.h"
#include "probe_core.h"
#include <stdio.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

/* Host-only parser transport. GPIO stubs do not represent an attached target. */
int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    uint8_t header[2], request[TB_DAP_PACKET], response[TB_DAP_PACKET];
    DAP_Setup();
    while (fread(header, 1, 2, stdin) == 2) {
        size_t size = tb_u16(header);
        if (size > sizeof request || fread(request, 1, size, stdin) != size)
            return 2;
        if (!tb_dap_validate(request, size, 4000000)) {
            fprintf(stderr, "Rejected DAP command 0x%02x, length %zu\n", request[0], size);
            return 3;
        }
        size_t count = DAP_ExecuteCommand(request, response) & 0xffff;
        if (count > sizeof response)
            return 4;
        header[0] = (uint8_t)count;
        header[1] = (uint8_t)(count >> 8);
        if (fwrite(header, 1, 2, stdout) != 2 || fwrite(response, 1, count, stdout) != count || fflush(stdout))
            return 5;
    }
    return 0;
}
