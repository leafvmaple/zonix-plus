#include "../command.h"

int main(void) {
    char buffer[1024];
    for (;;) {
        long count = sys_read(STDIN_FD, buffer, sizeof(buffer));
        if (count == 0)
            return 0;
        if (count < 0 || command_write_all(STDOUT_FD, buffer, count) < 0)
            return 1;
    }
}
