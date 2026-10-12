#pragma once

// The build selects the initial console verbosity; all levels are recorded.
#ifndef CONFIG_BOOT_VERBOSE
#define CONFIG_BOOT_VERBOSE 0
#endif

inline constexpr bool BOOT_VERBOSE = CONFIG_BOOT_VERBOSE != 0;
