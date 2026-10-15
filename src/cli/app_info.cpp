#include "app_info.h"
#include "app_cli_util.h"
#include "serial_cli.h"

void print_build_info(void)
{
    cli_printf("\r\n");
    CLI_INFO("=== BUILD INFO ===");

    CLI_INFO("FW   : %s", FIRMWARE_VERSION);
    CLI_INFO("Git hash   : %s", APP_GIT_HASH);
    CLI_INFO("Build TS   : %s", APP_BUILD_TS);
    CLI_INFO("Build date : %s", APP_BUILD_DATE);
    CLI_INFO("Build time : %s", APP_BUILD_TIME);

#if defined(CLI_USE_COLORS) && CLI_USE_COLORS
    CLI_INFO("CLI colors: compiled");
#else
    CLI_INFO("CLI colors: disabled");
#endif

    CLI_INFO("==================");
}
