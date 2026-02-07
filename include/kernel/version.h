#pragma once

/*
 * Zonix OS Version Information
 * 
 * Update MAJOR/MINOR/PATCH when releasing a new version.
 * VERSION_STRING will be automatically generated.
 */

#define ZONIX_VERSION_MAJOR     0
#define ZONIX_VERSION_MINOR     7
#define ZONIX_VERSION_PATCH     1

/* Macro stringification helpers */
#define _ZONIX_STR(x)           #x
#define ZONIX_STR(x)            _ZONIX_STR(x)

/* Auto-generated version string from MAJOR.MINOR.PATCH */
#define ZONIX_VERSION_STRING    \
    ZONIX_STR(ZONIX_VERSION_MAJOR) "." \
    ZONIX_STR(ZONIX_VERSION_MINOR) "." \
    ZONIX_STR(ZONIX_VERSION_PATCH)

#define ZONIX_CODENAME          "Genesis"

/* Build information - auto-updated during compilation */
#define ZONIX_BUILD_DATE        "Feb 05 2026"
#define ZONIX_BUILD_TIME        "15:33:35"
#define ZONIX_BUILD_NUMBER      1

