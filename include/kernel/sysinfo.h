#ifndef __KERNEL_SYSINFO_H__
#define __KERNEL_SYSINFO_H__

#include <kernel/version.h>

/*
 * System Information
 * 
 * Contains general system and hardware information
 */

/* System identification */
#define SYSINFO_NAME            "Zonix"
#define SYSINFO_DESCRIPTION     "Zonix Operating System"

/* Network/Host information */
#define SYSINFO_HOSTNAME        "zohar"
#define SYSINFO_DOMAIN          "local"

/* Hardware/Architecture information */
#define SYSINFO_ARCH            "x86_64"
#define SYSINFO_MACHINE         "x86_64"
#define SYSINFO_PLATFORM        "PC"

/* Formatted version string for display (like Ubuntu: #1 SMP Mon Oct 14 12:34:56 UTC 2025) */
#define SYSINFO_VERSION         "#" ZONIX_STR(ZONIX_BUILD_NUMBER) " " ZONIX_BUILD_DATE " " ZONIX_BUILD_TIME

/* Full system string (like uname -a) */
#define SYSINFO_FULL            SYSINFO_NAME " " SYSINFO_HOSTNAME " " \
                                ZONIX_VERSION_STRING " " SYSINFO_VERSION " " \
                                SYSINFO_MACHINE

#endif /* __KERNEL_SYSINFO_H__ */
