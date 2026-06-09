#pragma once

// Board pin-map selector. Picks the right board variant header based
// on the Kconfig choice in Kconfig.projbuild.

#include "sdkconfig.h"

#if defined(CONFIG_BIMMERZ_BOARD_WAVESHARE)
    #include "waveshare.h"
#elif defined(CONFIG_BIMMERZ_BOARD_DONGLE)
    #include "dongle.h"
#else
    #error "No Bimmerz board variant selected. Set CONFIG_BIMMERZ_BOARD_* in sdkconfig."
#endif
