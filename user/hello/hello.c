#include "minilib.h"
int main() {
    char* message = "Hello from Zonix userspace!\n";
    if (sys_write(STDOUT_FD, message, strlen(message)) == strlen(message))
        return 0;
    return 1;
}
