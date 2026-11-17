#pragma once

namespace rootfs {

// Mount the base filesystem containing /sbin/init at "/". TEST_MODE retains
// the boot-disk root used by existing regression entry points.
int init();
// Optionally mount a separate application filesystem (with /bin) at /opt.
int init_apps();

}  // namespace rootfs
