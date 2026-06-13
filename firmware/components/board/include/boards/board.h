#pragma once

// Board pin-map selector. Picks the right board variant header based
// on the Kconfig choice in Kconfig.projbuild.

#include "sdkconfig.h"

#if defined(CONFIG_BIMMERZ_BOARD_WAVESHARE_P4_MODULE_DEV_KIT)
    #include "waveshare_p4_module_dev_kit.h"
#elif defined(CONFIG_BIMMERZ_BOARD_WAVESHARE_P4_WIFI6)
    #include "waveshare_p4_wifi6.h"
#elif defined(CONFIG_BIMMERZ_BOARD_DONGLE)
    #include "dongle.h"
#else
    #error "No Bimmerz board variant selected. Set CONFIG_BIMMERZ_BOARD_* in sdkconfig."
#endif
