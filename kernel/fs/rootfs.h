#pragma once

namespace rootfs {

// Scan all registered disk block devices and mount the first one that
// contains a recognised filesystem to "/". Called once during kernel init,
// after block devices and PCI drivers have been probed.
int init();

}  // namespace rootfs
