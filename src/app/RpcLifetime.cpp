#include "RpcLifetime.hpp"
#include <config/PlatformCompat.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <Arduino.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#else
#include <unistd.h>
#endif

namespace core::app {

// Called only while constructing an endpoint, never in a transport callback or
// persistence turn. Zero means unavailable; storage RPC then remains closed.
FLASHMEM uint64_t createRpcLifetime() {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    // i.MX RT1062 TRNG, using the SDK defaults supplied by Teensy's imxrt.h.
    // Configuration/read sequence: NXP MCUXpresso drivers/trng/fsl_trng.c.
    CCM_CCGR6 |= CCM_CCGR6_TRNG(CCM_CCGR_ON);
    TRNG_MCTL = TRNG_MCTL_PRGM | TRNG_MCTL_RST_DEF | TRNG_MCTL_ERR;
    TRNG_SCMISC = TRNG_SCMISC_RTY_CT(TRNG_DEFAULT_RETRY_COUNT) | TRNG_SCMISC_LRUN_MAX(TRNG_DEFAULT_RUN_MAX_LIMIT);
    TRNG_SCML = TRNG_SCML_MONO_MAX(TRNG_DEFAULT_MONOBIT_MAXIMUM) | TRNG_SCML_MONO_RNG(TRNG_DEFAULT_MONOBIT_MAXIMUM - TRNG_DEFAULT_MONOBIT_MINIMUM);
    TRNG_SCR1L = TRNG_SCR1L_RUN1_MAX(TRNG_DEFAULT_RUNBIT1_MAXIMUM) | TRNG_SCR1L_RUN1_RNG(TRNG_DEFAULT_RUNBIT1_MAXIMUM - TRNG_DEFAULT_RUNBIT1_MINIMUM);
    TRNG_SCR2L = TRNG_SCR2L_RUN2_MAX(TRNG_DEFAULT_RUNBIT2_MAXIMUM) | TRNG_SCR2L_RUN2_RNG(TRNG_DEFAULT_RUNBIT2_MAXIMUM - TRNG_DEFAULT_RUNBIT2_MINIMUM);
    TRNG_SCR3L = TRNG_SCR3L_RUN3_MAX(TRNG_DEFAULT_RUNBIT3_MAXIMUM) | TRNG_SCR3L_RUN3_RNG(TRNG_DEFAULT_RUNBIT3_MAXIMUM - TRNG_DEFAULT_RUNBIT3_MINIMUM);
    TRNG_SCR4L = TRNG_SCR4L_RUN4_MAX(TRNG_DEFAULT_RUNBIT4_MAXIMUM) | TRNG_SCR4L_RUN4_RNG(TRNG_DEFAULT_RUNBIT4_MAXIMUM - TRNG_DEFAULT_RUNBIT4_MINIMUM);
    TRNG_SCR5L = TRNG_SCR5L_RUN5_MAX(TRNG_DEFAULT_RUNBIT5_MAXIMUM) | TRNG_SCR5L_RUN5_RNG(TRNG_DEFAULT_RUNBIT5_MAXIMUM - TRNG_DEFAULT_RUNBIT5_MINIMUM);
    TRNG_SCR6PL = TRNG_SCR6PL_RUN6P_MAX(TRNG_DEFAULT_RUNBIT6PLUS_MAXIMUM) | TRNG_SCR6PL_RUN6P_RNG(TRNG_DEFAULT_RUNBIT6PLUS_MAXIMUM - TRNG_DEFAULT_RUNBIT6PLUS_MINIMUM);
    TRNG_PKRMAX = TRNG_DEFAULT_POKER_MAXIMUM;
    TRNG_PKRRNG = TRNG_DEFAULT_POKER_MAXIMUM - TRNG_DEFAULT_POKER_MINIMUM;
    TRNG_FRQMAX = TRNG_DEFAULT_FREQUENCY_MAXIMUM; TRNG_FRQMIN = TRNG_DEFAULT_FREQUENCY_MINIMUM;
    TRNG_SDCTL = TRNG_SDCTL_ENT_DLY(TRNG_DEFAULT_ENTROPY_DELAY) | TRNG_SDCTL_SAMP_SIZE(TRNG_DEFAULT_SAMPLE_SIZE);
    TRNG_SBLIM = TRNG_DEFAULT_SPARSE_BIT_LIMIT;
    TRNG_MCTL = TRNG_MCTL_SAMP_MODE(2) | TRNG_MCTL_TRNG_ACC;
    (void)TRNG_ENT15; (void)TRNG_ENT0; // Discard stale block; SDK ENT_VAL workaround.
    const auto started = millis();
    while (!(TRNG_MCTL & (TRNG_MCTL_ENT_VAL | TRNG_MCTL_ERR)) && uint32_t(millis() - started) < 100) {}
    const auto state = TRNG_MCTL;
    uint64_t lifetime = 0;
    if ((state & TRNG_MCTL_ENT_VAL) && !(state & TRNG_MCTL_ERR)) {
        const uint32_t low = TRNG_ENT0, high = TRNG_ENT1;
        lifetime = uint64_t(low) | uint64_t(high) << 32;
    }
    TRNG_MCTL = TRNG_MCTL_PRGM;
    const auto stopped = millis();
    while (!(TRNG_MCTL & TRNG_MCTL_TSTOP_OK) && uint32_t(millis() - stopped) < 10) {}
    if (TRNG_MCTL & TRNG_MCTL_TSTOP_OK) CCM_CCGR6 &= ~CCM_CCGR6_TRNG(CCM_CCGR_ON);
    return lifetime;
#elif defined(_WIN32)
    // Keep platform linking local to this provider for all Core consumers.
    const auto library = LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return 0;
    const auto address = GetProcAddress(library, "BCryptGenRandom");
    decltype(&BCryptGenRandom) generate = nullptr;
    static_assert(sizeof(generate) == sizeof(address));
    std::memcpy(&generate, &address, sizeof(generate));
    uint64_t lifetime = 0;
    const bool ok = generate && generate(nullptr, reinterpret_cast<PUCHAR>(&lifetime), sizeof(lifetime), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
    FreeLibrary(library);
    return ok ? lifetime : 0;
#else
    uint64_t lifetime = 0;
    return getentropy(&lifetime, sizeof(lifetime)) == 0 ? lifetime : 0;
#endif
}

} // namespace core::app
