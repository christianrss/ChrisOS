/* KERNEL_SELFHOST_LEVEL_0
 * A function, literal outb, and an integer return.
 * This is not kernel/metal/serial.c. Includes, macros, static, and
 * control flow are rejected.
 */
void kstart(void) {
    outb(1016, 3);
    outb(1016 + 1, 0);
    return 0;
}
