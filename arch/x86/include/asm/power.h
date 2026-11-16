#pragma once

#include "lib/result.h"

// Final platform power request. Storage must already be quiesced and durable.
Error arch_power_off();
