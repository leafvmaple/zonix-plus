#pragma once

#include <kernel/version.h>

#define SYSINFO_NAME        "Zonix"
#define SYSINFO_DESCRIPTION "Zonix Operating System"

#define SYSINFO_HOSTNAME "zohar"
#define SYSINFO_DOMAIN   "local"

#define SYSINFO_ARCH     "x86_64"
#define SYSINFO_MACHINE  "x86_64"
#define SYSINFO_PLATFORM "PC"

#define SYSINFO_VERSION "#" ZONIX_STR(ZONIX_BUILD_NUMBER) " " ZONIX_BUILD_DATE " " ZONIX_BUILD_TIME

#define SYSINFO_FULL SYSINFO_NAME " " SYSINFO_HOSTNAME " " ZONIX_VERSION_STRING " " SYSINFO_VERSION " " SYSINFO_MACHINE
