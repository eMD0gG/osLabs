#include <stdio.h>
#include "lib.h"

int main() {
    proc_t p;

    if (run_background("whoami", &p) != 0) {
        printf("The program failed to start\n");
    }

    printf("The program is running in the background\n");

    int code = wait_for(p);
    printf("The process has ended, code: %d\n", code);

    return 0;
}