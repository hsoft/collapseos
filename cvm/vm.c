#include <stdio.h>
#include <string.h>
#include "vm.h"

// Port for block reads. Each read or write has to be done in 5 IO writes:
// 1 - r/w. 1 for read, 2 for write.
// 2 - blkid MSB
// 3 - blkid LSB
// 4 - dest addr MSB
// 5 - dest addr LSB
#define BLK_PORT 0x03

#ifndef BLKFS_PATH
#error BLKFS_PATH needed
#endif
#ifndef FBIN_PATH
#error FBIN_PATH needed
#endif

static VM vm;
static uint64_t blkop = 0; // 5 bytes
static FILE *blkfp;

static byte io_read(word addr)
{
    addr &= 0xff;
    IORD fn = vm.iord[addr];
    if (fn != NULL) {
        return fn();
    } else {
        fprintf(stderr, "Out of bounds I/O read: %d\n", addr);
        return 0;
    }
}

static void io_write(word addr, byte val)
{
    addr &= 0xff;
    IOWR fn = vm.iowr[addr];
    if (fn != NULL) {
        fn(val);
    } else {
        fprintf(stderr, "Out of bounds I/O write: %d / %d (0x%x)\n", addr, val, val);
    }
}

static void iowr_blk(byte val)
{
    blkop <<= 8;
    blkop |= val;
    byte rw = blkop >> 32;
    if (rw) {
        word blkid = (blkop >> 16);
        word dest = blkop & 0xffff;
        blkop = 0;
        fseek(blkfp, blkid*1024, SEEK_SET);
        if (rw==2) { // write
            fwrite(&vm.mem[dest], 1024, 1, blkfp);
        } else { // read
            fread(&vm.mem[dest], 1024, 1, blkfp);
        }
    }
}

static word gw(word addr) { return vm.mem[addr+1] << 8 | vm.mem[addr]; }
static void sw(word addr, word val) {
    vm.mem[addr] = val;
    vm.mem[addr+1] = val >> 8;
}
static word pop() {
    if (vm.uflw) return 0;
    if (vm.SP >= SP_ADDR) { vm.uflw = true; }
    return vm.mem[vm.SP++] | vm.mem[vm.SP++] << 8;
}
static void push(word x) {
    vm.SP -= 2; sw(vm.SP, x);
    if (vm.SP < vm.minSP) { vm.minSP = vm.SP; }
}
static word popRS() {
    if (vm.uflw) return 0;
    if (vm.RS <= RS_ADDR) { vm.uflw = true; }
    word x = gw(vm.RS); vm.RS -= 2; return x;
}
static void pushRS(word val) {
    vm.RS += 2; sw(vm.RS, val);
    if (vm.RS > vm.maxRS) { vm.maxRS = vm.RS; }
}
static void execute(word wordref) {
    byte wtype = vm.mem[wordref];
    if (wtype == 0) { // native
        vm.nativew[vm.mem[wordref+1]]();
    } else if (wtype == 1) { // compiled
        pushRS(vm.IP);
        vm.IP = wordref+1;
    } else { // cell or does
        push(wordref+1);
        if (wtype == 3) {
            pushRS(vm.IP);
            vm.IP = gw(wordref+3);
        }
    }
}
static word find(word daddr, word waddr) {
    byte len = vm.mem[waddr];
    while (1) {
        if ((vm.mem[daddr-1] & 0x7f) == len) {
            if (strncmp(&vm.mem[waddr+1], &vm.mem[daddr-3-len], len) == 0) {
                return daddr;
            }
        }
        daddr -= 3;
        word offset = gw(daddr);
        if (offset) {
            daddr -= offset;
        } else {
            return 0;
        }
    }
}

static void EXIT() { vm.IP = popRS(); }
static void _br_() {
    word off = vm.mem[vm.IP];
    if (off > 0x7f ) { off -= 0x100; }
    vm.IP += off;
}
static void _cbr_() { if (!pop()) { _br_(); } else { vm.IP++; } }
static void _loop_() {
    word I = gw(vm.RS); I++; sw(vm.RS, I);
    if (I == gw(vm.RS-2)) { // don't branch
        popRS(); popRS();
        vm.IP++;
    } else { // branch
        _br_();
    }
}
static void SP_to_R_2() { word x = pop(); pushRS(pop()); pushRS(x); }
static void nlit() { push(gw(vm.IP)); vm.IP += 2; }
static void slit() { push(vm.IP); vm.IP += vm.mem[vm.IP] + 1; }
static void SP_to_R() { pushRS(pop()); }
static void R_to_SP() { push(popRS()); }
static void R_to_SP_2() { word x = popRS(); push(popRS()); push(x); }
static void EXECUTE() { execute(pop()); }
static void ROT() { // a b c -- b c a
    word c = pop(); word b = pop(); word a = pop();
    push(b); push(c); push(a);
}
static void DUP() { // a -- a a
    word a = pop(); push(a); push(a);
}
static void CDUP() {
    word a = pop(); push(a); if (a) { push(a); }
}
static void DROP() { pop(); }
static void SWAP() { // a b -- b a
    word b = pop(); word a = pop();
    push(b); push(a);
}
static void OVER() { // a b -- a b a
    word b = pop(); word a = pop();
    push(a); push(b); push(a);
}
static void PICK() {
    word x = pop();
    push(gw(vm.SP+x*2));
}
static void _roll_() { //   "1 2 3 4 4 (roll)" --> "1 3 4 4"
    word x = pop();
    while (x) { vm.mem[vm.SP+x+2] = vm.mem[vm.SP+x]; x--; }
}
static void DROP2() { pop(); pop(); }
static void DUP2() { // a b -- a b a b
    word b = pop(); word a = pop();
    push(a); push(b); push(a); push(b);
}
static void S0() { push(SP_ADDR); }
static void Saddr() { push(vm.SP); }
static void AND() { push(pop() & pop()); }
static void OR() { push(pop() | pop()); }
static void XOR() { push(pop() ^ pop()); }
static void NOT() { push(!pop()); }
static void PLUS() { push(pop() + pop()); }
static void MINUS() {
    word b = pop(); word a = pop();
    push(a - b);
}
static void MULT() { push(pop() * pop()); }
static void DIVMOD() {
    word b = pop(); word a = pop();
    push(a % b); push(a / b);
}
static void STORE() {
    word a = pop(); word val = pop();
    sw(a, val);
}
static void FETCH() { push(gw(pop())); }
static void CSTORE() {
    word a = pop(); word val = pop();
    vm.mem[a] = val;
}
static void CFETCH() { push(vm.mem[pop()]); }
static void IO_OUT() {
    word a = pop(); word val = pop();
    io_write(a, val);
}
static void IO_IN() { push(io_read(pop())); }
static void RI() { push(gw(vm.RS)); }
static void RI_() { push(gw(vm.RS-2)); }
static void RJ() { push(gw(vm.RS-4)); }
static void BYE() { vm.running = false; }
static void _resSP_() { vm.SP = SP_ADDR; }
static void _resRS_() { vm.RS = RS_ADDR; }
static void Seq() {
    word s1 = pop(); word s2 = pop();
    byte len = vm.mem[s1];
    if (len == vm.mem[s2]) {
        s1++; s2++;
        push(strncmp(&vm.mem[s1], &vm.mem[s2], len) == 0);
    } else {
        push(0);
    }
}
static void CMP() {
    word b = pop(); word a = pop();
    if (a == b) { push(0); } else if (a > b) { push(1); } else { push(-1); }
}
static void _find() {
    word waddr = pop(); word daddr = pop();
    daddr = find(daddr, waddr);
    if (daddr) {
        push(daddr); push(1);
    } else {
        push(waddr); push(0);
    }
}
static void ZERO() { push(0); }
static void ONE() { push(1); }
static void MONE() { push(-1); }
static void PLUS1() { push(pop()+1); }
static void MINUS1() { push(pop()-1); }
static void MINUS2() { push(pop()-2); }
static void PLUS2() { push(pop()+2); }
static void RSHIFT() { word u = pop(); push(pop()>>u); }
static void LSHIFT() { word u = pop(); push(pop()<<u); }
static void native(NativeWord func) {
    vm.nativew[vm.nativew_count++] = func;
}

VM* VM_init() {
    fprintf(stderr, "Using blkfs %s\n", BLKFS_PATH);
    blkfp = fopen(BLKFS_PATH, "r+");
    if (!blkfp) {
        fprintf(stderr, "Can't open\n");
        return NULL;
    }
    fseek(blkfp, 0, SEEK_END);
    if (ftell(blkfp) < 100 * 1024) {
        fclose(blkfp);
        fprintf(stderr, "emul/blkfs too small, something's wrong, aborting.\n");
        return NULL;
    }
    fseek(blkfp, 0, SEEK_SET);
    // initialize memory
    memset(vm.mem, 0, 0x10000);
    FILE *bfp = fopen(FBIN_PATH, "r");
    if (!bfp) {
        fprintf(stderr, "Can't open forth.bin\n");
        return NULL;
    }
    int i = 0;
    int c = getc(bfp);
    while (c != EOF) {
        vm.mem[i++] = c;
        c = getc(bfp);
    }
    fclose(bfp);
    vm.SP = SP_ADDR;
    vm.RS = RS_ADDR;
    vm.minSP = SP_ADDR;
    vm.maxRS = RS_ADDR;
    vm.nativew_count = 0;
    for (int i=0; i<0x100; i++) {
        vm.iord[i] = NULL;
        vm.iowr[i] = NULL;
    }
    vm.iowr[BLK_PORT] = iowr_blk;
    native(EXIT);
    native(_br_);
    native(_cbr_);
    native(_loop_);
    native(SP_to_R_2);
    native(nlit);
    native(slit);
    native(SP_to_R);
    native(R_to_SP);
    native(R_to_SP_2);
    native(EXECUTE);
    native(ROT);
    native(DUP);
    native(CDUP);
    native(DROP);
    native(SWAP);
    native(OVER);
    native(PICK);
    native(_roll_);
    native(DROP2);
    native(DUP2);
    native(S0);
    native(Saddr);
    native(AND);
    native(OR);
    native(XOR);
    native(NOT);
    native(PLUS);
    native(MINUS);
    native(MULT);
    native(DIVMOD);
    native(STORE);
    native(FETCH);
    native(CSTORE);
    native(CFETCH);
    native(IO_OUT);
    native(IO_IN);
    native(RI);
    native(RI_);
    native(RJ);
    native(BYE);
    native(_resSP_);
    native(_resRS_);
    native(Seq);
    native(CMP);
    native(_find);
    native(ZERO);
    native(ONE);
    native(MONE);
    native(PLUS1);
    native(MINUS1);
    native(PLUS2);
    native(MINUS2);
    native(RSHIFT);
    native(LSHIFT);
    vm.IP = gw(0x04) + 1; // BOOT
    sw(SYSVARS+0x02, gw(0x08)); // CURRENT
    sw(SYSVARS+0x04, gw(0x08)); // HERE
    vm.uflw = false;
    vm.running = true;
    return &vm;
}

void VM_deinit()
{
    fclose(blkfp);
}

bool VM_steps(int n) {
    if (!vm.running) {
        fprintf(stderr, "machine halted!\n");
        return false;
    }
    while (n && vm.running) {
        word wordref = gw(vm.IP);
        vm.IP += 2;
        execute(wordref);
        if (vm.uflw) {
            vm.uflw = false;
            execute(gw(0x06)); /* uflw */
        }
        n--;
    }
    return vm.running;
}

void VM_memdump() {
    fprintf(stderr, "Dumping memory to memdump. IP %04x\n", vm.IP);
    FILE *fp = fopen("memdump", "w");
    fwrite(vm.mem, 0x10000, 1, fp);
    fclose(fp);
}

void VM_debugstr(char *s) {
    sprintf(s, "SP %04x (%04x) RS %04x (%04x)",
        vm.SP, vm.minSP, vm.RS, vm.maxRS);
}

void VM_printdbg() {
    char buf[0x100];
    VM_debugstr(buf);
    fprintf(stderr, "%s\n", buf);
}