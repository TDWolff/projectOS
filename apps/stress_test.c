#include "libapp.h"

void _start() {
    // We can now print from the user app!
    sys_kprintf("Hello from user space! This is stress_test.pexe speaking.\n");
    sys_kprintf("Starting stress test...\n");

    sys_exit();
}