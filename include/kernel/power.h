#pragma once

#include "lib/result.h"

namespace power {

// Request platform power-off. Success does not return; failure leaves the
// caller running with its interrupt state restored.
Error shutdown();

}  // namespace power
