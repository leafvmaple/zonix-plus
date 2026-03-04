#!/bin/bash
# Update build date and time in version.h

VERSION_FILE="include/kernel/version.h"

if [ ! -f "$VERSION_FILE" ]; then
    echo "Error: $VERSION_FILE not found!"
    exit 1
fi

# Get current date in format "Mon DD YYYY"
BUILD_DATE=$(date "+%b %d %Y")

# Get current time in format "HH:MM:SS"
BUILD_TIME=$(date "+%H:%M:%S")

# Update the BUILD_DATE line
sed -i "s/^#define ZONIX_BUILD_DATE.*/#define ZONIX_BUILD_DATE        \"$BUILD_DATE\"/" "$VERSION_FILE"

# Update the BUILD_TIME line
sed -i "s/^#define ZONIX_BUILD_TIME.*/#define ZONIX_BUILD_TIME        \"$BUILD_TIME\"/" "$VERSION_FILE"

echo "Updated BUILD_DATE to: $BUILD_DATE"
echo "Updated BUILD_TIME to: $BUILD_TIME"
