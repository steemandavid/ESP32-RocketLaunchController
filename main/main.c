/**
 * ESP32 Wireless Rocket Launch Controller
 * Main entry point — branches on CONFIG_RLC_UNIT_TYPE.
 */

#include <stdio.h>
#include "rlc_version.h"

#if defined(CONFIG_RLC_UNIT_BASE)
#include "rlc_base.h"
#elif defined(CONFIG_RLC_UNIT_REMOTE)
#include "rlc_remote.h"
#else
#error "CONFIG_RLC_UNIT_TYPE must be set to BASE or REMOTE"
#endif

void app_main(void)
{
    printf("RLC Firmware v%s\n", RLC_VERSION_STRING);

#if defined(CONFIG_RLC_UNIT_BASE)
    base_app_main();
#elif defined(CONFIG_RLC_UNIT_REMOTE)
    remote_app_main();
#endif
}
