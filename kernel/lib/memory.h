#pragma once

#include <base/types.h>
#include <asm/arch.h>
#include <sys/new.hpp>
#include <sys/cstring.hpp>

// Byte primitives use the runtime ABI declared by sys/cstring.hpp.
#include "mm/heap.h"

// sys/new.hpp owns the shared allocation declarations and placement definitions.
// The kernel provides the allocating/freeing definitions in cxxrt.cpp.
