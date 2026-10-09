#!/bin/bash
# Update build date and time in version.h

version_file="include/kernel/version.h"

if [ ! -f "$version_file" ]; then
    echo "Error: $version_file not found!"
    exit 1
fi

# Get current date in format "Mon DD YYYY"
build_date=$(date "+%b %d %Y")

# Get current time in format "HH:MM:SS"
build_time=$(date "+%H:%M:%S")

# Update the BUILD_DATE line
sed -i "s/^#define ZONIX_BUILD_DATE.*/#define ZONIX_BUILD_DATE        \"$build_date\"/" "$version_file"

# Update the BUILD_TIME line
sed -i "s/^#define ZONIX_BUILD_TIME.*/#define ZONIX_BUILD_TIME        \"$build_time\"/" "$version_file"

echo "Updated BUILD_DATE to: $build_date"
echo "Updated BUILD_TIME to: $build_time"
