#pragma once

namespace jh7110_gmac {

// Offers the board's Ethernet port as /dev/net0; does nothing on boards
// without one. Returns 0 or a negative error.
int init();

}  // namespace jh7110_gmac
