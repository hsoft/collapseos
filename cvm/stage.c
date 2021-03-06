#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "vm.h"

#ifndef BLKFS_PATH
#error BLKFS_PATH needed
#endif
#ifndef FBIN_PATH
#error FBIN_PATH needed
#endif
#define RAMSTART 0
#define STDIO_PORT 0x00
// To know which part of RAM to dump, we listen to port 2, which at the end of
// its compilation process, spits its HERE addr to port 2 (MSB first)
#define HERE_PORT 0x02

VM *vm;
// We support double-pokes, that is, a first poke to tell where to start the
// dump and a second one to tell where to stop. If there is only one poke, it's
// then ending HERE and we start at sizeof(KERNEL).
static uint16_t start_here = 0;
static uint16_t end_here = 0;

static uint8_t iord_stdio()
{
    int c = getc(stdin);
    if (c == EOF) {
        vm->running = false;
    }
    return (uint8_t)c;
}

static void iowr_stdio(uint8_t val)
{
    // comment if you don't like verbose staging output
    putc(val, stderr);
}

static void iowr_here(uint8_t val)
{
    start_here <<=8;
    start_here |= (end_here >> 8);
    end_here <<= 8;
    end_here |= val;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        vm = VM_init(FBIN_PATH, BLKFS_PATH);
    } else {
        vm = VM_init(FBIN_PATH, argv[1]);
    }
    if (vm == NULL) {
        return 1;
    }
    vm->iord[STDIO_PORT] = iord_stdio;
    vm->iowr[STDIO_PORT] = iowr_stdio;
    vm->iowr[HERE_PORT] = iowr_here;
    while (VM_steps(1));

    // We're done, now let's spit dict data
    for (int i=start_here; i<end_here; i++) {
        putchar(vm->mem[i]);
    }
    VM_printdbg();
    VM_deinit();
    return 0;
}
