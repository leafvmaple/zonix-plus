#pragma once

// The stack each architecture's head.S boots on. The idle task keeps running
// on it, so it is never freed.
extern "C" char __boot_stack_bottom[];
extern "C" char __boot_stack_top[];
