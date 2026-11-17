#pragma once

#define TERMINAL_GET_SIZE 0x5601
#define TERMINAL_SET_RAW  0x5602

/* SET_RAW uses an immediate 0/1 argument. Raw mode passes Ctrl-C to the
 * foreground application; ownership ends on exit or foreground handoff. */
struct TerminalSize {
    unsigned int rows;
    unsigned int cols;
};
