( ----- 600 )
TRS-80 Recipe

Support code for the TRS-80 recipe. Contains drivers for the
keyboard, video and floppy. At the moment, they are thin layer
over the drivers provided by TRSDOS' SVC.

Load with "602 LOAD".

There is also the RECV program at B612.
( ----- 602 )
1 9 LOADR+
( ----- 603 )
CODE (key?) ( -- c? f )
    A 0x08 LDri, ( @KBD )
    0x28 RST,
    IFZ, PUSHA, THEN, PUSHZ,
;CODE
CODE (emit) EXX, ( protect BC )
    BC POP, ( c == @DSP arg ) chkPS,
    A 0x02 LDri, ( @DSP )
    0x28 RST,
EXX, ( unprotect BC ) ;CODE
CODE AT-XY EXX, ( protect BC )
    DE POP, H E LDrr, ( Y )
    DE POP, L E LDrr, ( X ) chkPS,
    A 0x0f LDri, ( @VDCTL ) B 3 LDri, ( setcur )
    0x28 RST,
EXX, ( unprotect BC ) ;CODE
( ----- 604 )
: LINES 24 ; : COLS 80 ;
: XYMODE 0x70 RAM+ ;
: CELL! COLS /MOD AT-XY (emit) ;
CODE BYE
    HL 0 LDdi,
    A 0x16 LDri, ( @EXIT )
    0x28 RST,
CODE @DCSTAT ( drv -- f ) EXX, ( protect BC )
    BC POP,
    chkPS,
    A 0x28 LDri, ( @DCSTAT )
    0x28 RST,
    PUSHZ,
EXX, ( unprotect BC ) ;CODE
( ----- 605 )
CODE @RDSEC ( drv cylsec addr -- f ) EXX, ( protect BC )
    HL POP,
    DE POP,
    BC POP,
    chkPS,
    A 0x31 LDri, ( @RDSEC )
    0x28 RST,
    PUSHZ,
EXX, ( unprotect BC ) ;CODE
( ----- 606 )
CODE @WRSEC ( drv cylsec addr -- f ) EXX, ( protect BC )
    HL POP,
    DE POP,
    BC POP,
    chkPS,
    A 0x35 LDri, ( @WRSEC )
    0x28 RST,
    PUSHZ,
EXX, ( unprotect BC ) ;CODE
CODE @GET ( a -- c f )
    DE POP,
    chkPS,
    A 0x03 LDri, ( @GET )
    0x28 RST,
    PUSHA, PUSHZ,
;CODE
( ----- 607 )
CODE @PUT ( c a -- f ) EXX, ( protect BC )
    DE POP,
    BC POP,
    chkPS,
    A 0x04 LDri, ( @PUT )
    0x28 RST,
    PUSHZ,
EXX, ( unprotect BC ) ;CODE
( ----- 609 )
: _err LIT" FDerr" ERR ;
: _cylsec ( sec -- cs, return sector/cylinder for given secid )
    ( 4 256b sectors per block, 10 sec per cyl, 40 cyl max )
    10 /MOD ( sec cyl )
    DUP 39 > IF _err THEN
    8 LSHIFT + ( cylsec )
;
: FD@! ( wref blk -- )
    1 @DCSTAT NOT IF _err THEN
    2 LSHIFT ( 4 * -- wr sec )
    4 0 DO ( wr sec )
        DUP I + _cylsec ( wr sec cs )
        I 8 LSHIFT BLK( + ( wr sec cs addr )
        1 ROT ROT ( wr sec drv cs addr )
        4 PICK EXECUTE NOT IF _err THEN
    LOOP 2DROP ;
( ----- 610 )
: FD@ ['] @RDSEC SWAP FD@! ;
: FD! ['] @WRSEC SWAP FD@! ;
: FD$ ['] FD@ ['] BLK@* **! ['] FD! ['] BLK!* **! ;

( ----- 611 )
: CL$ ( baudcode -- )
0x02 0xe8 PC! ( UART RST ) DUP 4 LSHIFT OR 0xe9 PC! ( bauds )
    0b01101101 0xea PC! ( word8 no parity no-RTS ) ;
: CL> BEGIN 0xea PC@ 0x40 AND UNTIL 0xeb PC! ;
CODE _
    0xea INAi, 0x80 ANDi, IFZ, PUSH0, ELSE,
    0xeb INAi, PUSHA, PUSH1, THEN,
;CODE
: CL<? _ IF 1 EXIT THEN
    0b01101100 0xea PC! ( RTS )
    1 TICKS ( 100 us ) _
    0b01101101 0xea PC! ( no-RTS ) ;
( ----- 612 )
( We process the 0x20 exception by pre-putting a mask in the
  (HL) we're going to write to. If it wasn't a 0x20, we put a
  0xff mask. If it was a 0x20, we put a 0x7f mask. )
: @GET,
    A 0x03 LDri, ( @GET )
    DE COM_DRV_ADDR LDdi,
    0x28 RST, JRNZ, L2 FWR ( maybeerror )
    A ORr,
    CZ RETc, ( Sending a straight NULL ends the comm. ) ;
: @PUT, ( @PUT that char back )
        C A LDrr,
        A 0x04 LDri, ( @PUT )
        0x28 RST, JRNZ, L3 FWR ( error )
        A C LDrr, ;
H@ ORG !
HL DEST_ADDR LDdi,                                    ( cont. )
( ----- 613 )
BEGIN,
    A 0xff LDri, (HL) A LDrr, ( default mask )
    L1 BSET ( loop2 ) @GET, @PUT,
    0x20 CPi, JRZ, L4 FWR ( escapechar )
	( not an escape char, just apply the mask and write )
    (HL) ANDr, (HL) A LDrr,
    HL INCd,
JR, AGAIN,
L4 FSET ( escapechar, adjust by setting (hl) to 0x7f )
7 (HL) RES, JR, L1 BWR ( loop2 )
L2 FSET ( maybeerror, was it an error? )
A ORr, JRZ, L1 BWR ( loop2, not an error )
L3 FSET ( error )
C A LDrr, ( error code from @GET/@PUT )
A 0x1a LDri, ( @ERROR ) 0x28 RST, RET,
