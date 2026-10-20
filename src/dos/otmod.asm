; =====================================================================
; OTMOD.COM  -  Oregon Trail Accessibility Mod : resident DOS helper
; ---------------------------------------------------------------------
; Loaded in DOSBox before the (patched) game, OREGONM.EXE.
;
;  * INT 61h  : hook dispatcher. The patched game executes "CD 61 id"
;               (not 66h: the game's DIGPAK/MIDPAK sound drivers own 66h)
;               at a handful of places (text drawing, dialog loading,
;               the event poll, hunting, rafting ...).  We record what
;               happened into a ring buffer inside our "mailbox".
;  * INT 1Ch  : timer tick.  Delivers input that the Windows companion
;               queued in the mailbox (keystrokes into the BIOS keyboard
;               buffer, mouse clicks into the game's own event queue).
;
; The Windows companion finds the mailbox by scanning DOSBox's memory
; for the signature below, then reads/writes it directly.
;
; Assemble:  nasm -f bin -o OTMOD.COM otmod.asm
; =====================================================================
        cpu     8086
        org     100h

RINGSIZE        equ     8192            ; must be a power of two
RINGMASK        equ     RINGSIZE-1
INQSIZE         equ     16              ; input queue entries (power of 2)
DTN             equ     12              ; open-dialog table entries
DTSZ            equ     20              ; bytes per entry

; relative segments inside OREGON.EXE (from the Borland segment table)
HOOKINT         equ     61h             ; must match tools/patch_exe.py
SEG_EVT         equ     107Ch           ; event queue code segment
SEG_TEXT        equ     17B8h           ; text output code segment
SEG_DGROUP      equ     1AB5h           ; data segment
EVPOST_OFF      equ     01C0h           ; evPostEvent(ev far*) in SEG_EVT

; game DGROUP offsets
DG_CURX         equ     5C68h           ; game's own cursor x
DG_CURY         equ     5C6Ah           ; game's own cursor y
DG_EVQMAX       equ     6AB0h           ; event queue capacity (0 = not ready)

; ring record types
R_TEXT          equ     1
R_CTR           equ     2
R_DLGRUN        equ     3
R_DLGFREE       equ     4
R_STATE         equ     6
R_HUNT          equ     7
R_RAFT          equ     8
R_DLGDRAW       equ     9

; input queue command types (written by the companion)
I_KEY           equ     1               ; a = BIOS key word (scan<<8 | ascii)
I_CLICK         equ     2               ; a = x, b = y (press now, release next tick)
I_MOVE          equ     3               ; a = x, b = y (set game cursor only)
I_PRESS         equ     4               ; a = x, b = y (press only)
I_RELEASE       equ     5               ; a = x, b = y (release only)

start:  jmp     install

; ---------------------------------------------------------------------
; Mailbox (layout is shared with the companion - keep in sync with
; src/companion/mailbox.h)
; ---------------------------------------------------------------------
        align   16
mbox:
mb_sig          db      'OTACCESS-MBOX-1',0     ; +00
mb_self         dd      0                       ; +10 linear address of mbox
mb_ver          dw      3                       ; +14 layout version
mb_cs_evt       dw      0                       ; +16 runtime CS of SEG_EVT
mb_dgroup       dw      0                       ; +18 runtime DGROUP segment
mb_heart        dw      0                       ; +1A INT 1Ch tick counter
mb_polls        dw      0                       ; +1C event-poll counter
mb_hold         dw      0                       ; +1E companion: 1 = freeze game at next poll
mb_rhead        dw      0                       ; +20 ring write offset
mb_rseq         dw      0                       ; +22 records written
mb_rsize        dw      RINGSIZE                ; +24
mb_roff         dw      ring-mbox               ; +26 ring offset from mbox
mb_state        dd      0                       ; +28 game state struct (off,seg)
mb_dlg          dd      0                       ; +2C dialog being run (off,seg)
mb_hunt         dd      0                       ; +30 hunting sprite table (off,seg)
mb_raft         dd      0                       ; +34 rafting object table (off,seg)
mb_inqh         dw      0                       ; +38 companion: input queue head
mb_inqt         dw      0                       ; +3A TSR: input queue tail
mb_holding      dw      0                       ; +3C 1 while the game is frozen
mb_relpend      dw      0                       ; +3E release event pending
mb_inq          times INQSIZE*8 db 0            ; +40 input queue (type,a,b,c words)
mb_relx         dw      0                       ; +C0
mb_rely         dw      0                       ; +C2
mb_lastid       dw      0                       ; +C4 last hook id (debug)
mb_hooks        dw      0                       ; +C6 total hook calls (debug)
mb_dropped      dw      0                       ; +C8 input commands dropped
mb_flags        dw      0                       ; +CA bit0 = text capture off
mb_dtoff        dw      dtab-mbox               ; +CC offset of the open-dialog table
mb_dtn          dw      DTN                     ; +CE entries in the table
mb_dseq         dw      0                       ; +D0 next dialog sequence number
                times   (100h - ($-mbox)) db 0
; open-dialog table: one 20-byte entry per loaded screen script
;   off(2) seg(2) seq(2) name(14, NUL padded); seg = 0 means free
dtab            times   DTN*DTSZ db 0           ; +100
                times   (200h - ($-mbox)) db 0
ring            times   RINGSIZE db 0           ; +200

; scratch event used for posting into the game's queue
evbuf           times   14 db 0

old1c           dd      0
old66           dd      0

; ---------------------------------------------------------------------
; ring_put: write one record.
;   in:  AL = type, BX = p1, CX = p2, DX = p3, [cs:rp4] = p4
;        DS:SI = text (NUL terminated) or SI = 0 for none
;   regs preserved except flags
; record: A5 type seq(2) p1(2) p2(2) p3(2) p4(2) len(1) text(len)
; ---------------------------------------------------------------------
rp4     dw      0
rtype   db      0
ring_put:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    cs
        pop     es
        mov     [cs:rtype],al
        mov     di,[cs:mb_rhead]
        mov     al,0A5h
        call    .putb
        mov     al,[cs:rtype]
        call    .putb
        mov     ax,[cs:mb_rseq]
        call    .putw
        mov     ax,bx
        call    .putw
        mov     ax,cx
        call    .putw
        mov     ax,dx
        call    .putw
        mov     ax,[cs:rp4]
        call    .putw
        ; length placeholder position
        push    di
        mov     al,0
        call    .putb
        xor     cx,cx                   ; length counter
        or      si,si
        jz      .nolen
.copy:  lodsb
        or      al,al
        jz      .nolen
        call    .putb
        inc     cx
        cmp     cx,250
        jb      .copy
.nolen: pop     bx                      ; offset of length byte
        mov     [es:ring+bx],cl
        mov     [cs:mb_rhead],di        ; publish
        inc     word [cs:mb_rseq]
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
.putb:  mov     [es:ring+di],al
        inc     di
        and     di,RINGMASK
        ret
.putw:  call    .putb
        mov     al,ah
        jmp     .putb


; ---------------------------------------------------------------------
; open-dialog table maintenance (lets a late-starting companion learn
; which screens are up even after the ring has wrapped)
; ---------------------------------------------------------------------
; dtab_add: BX = dialog off, CX = dialog seg, DS:SI = name.  All regs kept.
dtab_add:
        push    ax
        push    cx
        push    di
        push    si
        push    es
        push    cs
        pop     es
        push    cx
        call    dtab_find               ; reuse an entry for the same pointer
        pop     cx
        jnc     .have
        push    cx
        push    bx
        xor     bx,bx
        xor     cx,cx
        call    dtab_find               ; else the first free entry
        pop     bx
        pop     cx
        jc      .out                    ; table full
.have:  mov     [es:di],bx
        mov     [es:di+2],cx
        mov     ax,[cs:mb_dseq]
        inc     word [cs:mb_dseq]
        mov     [es:di+4],ax
        add     di,6
        mov     cx,13
.cp:    lodsb
        stosb
        or      al,al
        jz      .pad
        loop    .cp
        xor     al,al
        stosb
        jmp     .out
.pad:   dec     cx
        jz      .out
        rep     stosb
.out:   pop     es
        pop     si
        pop     di
        pop     cx
        pop     ax
        ret

; dtab_del: BX = off, CX = seg.  All regs kept.
dtab_del:
        push    di
        push    cx
        call    dtab_find
        jc      .no
        mov     word [cs:di],0
        mov     word [cs:di+2],0
.no:    pop     cx
        pop     di
        ret

; dtab_find: BX = off, CX = seg -> DI = entry (CF clear) or CF set.
dtab_find:
        push    ax
        mov     di,dtab
        mov     ax,DTN
.lp:    cmp     [cs:di],bx
        jne     .nx
        cmp     [cs:di+2],cx
        je      .hit
.nx:    add     di,DTSZ
        dec     ax
        jnz     .lp
        pop     ax
        stc
        ret
.hit:   pop     ax
        clc
        ret

; ---------------------------------------------------------------------
; hook dispatcher (INT HOOKINT).  Entry stack: IP(-> id byte), CS, FLAGS.
; ---------------------------------------------------------------------
; frame after "push bp / mov bp,sp":
;   [bp+0] caller BP   [bp+2] IP   [bp+4] CS   [bp+6] FLAGS
;   [bp+8] = caller SP before the INT (for prologue hooks this is the
;            hooked function's far return address; args start at +12)
int66:
        push    bp
        mov     bp,sp
        push    ds
        push    si
        push    ax
        lds     si,[bp+2]
        mov     al,[si]                 ; hook id
        inc     word [bp+2]             ; resume after the id byte
        xor     ah,ah
        mov     [cs:mb_lastid],ax
        inc     word [cs:mb_hooks]
        cmp     al,1
        je      h_text
        cmp     al,2
        je      h_ctr
        cmp     al,3
        je      h_dlgrun
        cmp     al,4
        je      h_dlgfree
        cmp     al,5
        je      h_evget
        cmp     al,6
        je      h_state
        cmp     al,7
        je      h_hunt
        cmp     al,8
        je      h_raft
        cmp     al,9
        je      h_dlgdraw
        jmp     plain_exit

; --- exits -----------------------------------------------------------
plain_exit:                             ; hooked bytes were NOP-padded
        pop     ax
        pop     si
        pop     ds
        pop     bp
        iret

prologue_exit:                          ; emulate "push bp / mov bp,sp"
        pop     ax
        pop     si
        pop     ds
        push    ax
        push    bx
        mov     ax,[bp+0]               ; caller BP
        mov     bx,[bp+2]
        mov     [bp+0],bx               ; IP
        mov     bx,[bp+4]
        mov     [bp+2],bx               ; CS
        mov     bx,[bp+6]
        mov     [bp+4],bx               ; FLAGS
        mov     [bp+6],ax               ; pushed BP
        pop     bx
        pop     ax
        add     bp,6                    ; BP = SP after IRET
        iret

; learn the game's load address from a hook in a known main-image segment
; in: AX = runtime CS, BX = relative segment of that code
learn_base:
        cmp     word [cs:mb_dgroup],0
        jne     .done
        push    ax
        sub     ax,bx
        push    ax
        add     ax,SEG_EVT
        mov     [cs:mb_cs_evt],ax
        pop     ax
        add     ax,SEG_DGROUP
        mov     [cs:mb_dgroup],ax
        pop     ax
.done:  ret

; --- 1: text drawn  (m17B8:00B1  y,x,str) -------------------------------
h_text:
        push    bx
        push    cx
        push    dx
        mov     ax,[bp+4]
        mov     bx,SEG_TEXT
        call    learn_base
        test    word [cs:mb_flags],1
        jnz     .skip
        mov     bx,[bp+14]              ; x
        mov     cx,[bp+12]              ; y
        mov     dx,[bp+8]               ; caller ret off
        mov     ax,[bp+10]
        mov     [cs:rp4],ax             ; caller ret seg
        lds     si,[bp+16]              ; string
        mov     al,R_TEXT
        call    ring_put
.skip:  pop     dx
        pop     cx
        pop     bx
        jmp     prologue_exit

; --- 2: CTR loaded (o12:0E17, replaces mov dx,[bp-2e] / mov ax,[bp-30]) --
h_ctr:
        push    bx
        push    cx
        push    di
        mov     di,[bp+0]               ; loader's BP
        mov     bx,[ss:di-30h]          ; dialog offset
        mov     cx,[ss:di-2Eh]          ; dialog segment
        mov     word [cs:rp4],0
        lds     si,[ss:di+6]            ; CTR file name
        mov     al,R_CTR
        mov     dx,0
        call    ring_put
        call    dtab_add                ; BX:CX = dialog, DS:SI = name
        mov     dx,cx                   ; emulate: DX = seg
        mov     [bp-6],bx               ; saved AX slot = off
        pop     di
        pop     cx
        pop     bx
        jmp     plain_exit

; --- 3: DoDialog(dlg)  (o12:1527) -------------------------------------
h_dlgrun:
        push    bx
        push    cx
        push    dx
        mov     bx,[bp+12]
        mov     cx,[bp+14]
        mov     [cs:mb_dlg],bx
        mov     [cs:mb_dlg+2],cx
        xor     dx,dx
        mov     [cs:rp4],dx
        xor     si,si
        mov     al,R_DLGRUN
        call    ring_put
        pop     dx
        pop     cx
        pop     bx
        jmp     prologue_exit

; --- 4: FreeDialog(dlg)  (o12:0E4D) -----------------------------------
h_dlgfree:
        push    bx
        push    cx
        push    dx
        mov     bx,[bp+12]
        mov     cx,[bp+14]
        xor     dx,dx
        mov     [cs:rp4],dx
        xor     si,si
        mov     al,R_DLGFREE
        call    ring_put
        call    dtab_del                ; BX = off, CX = seg
        pop     dx
        pop     cx
        pop     bx
        jmp     prologue_exit

; --- 9: DrawDialog(dlg)  (o12:0F72) : which loaded screen is on top ---
; Screens are often loaded ahead of time (an error box with its file
; dialog); drawing is what shows one.  Repeat draws of the same dialog are
; not recorded, so redraw loops do not flood the ring.
lastdraw        dd      0
h_dlgdraw:
        push    bx
        push    cx
        push    dx
        mov     bx,[bp+12]
        mov     cx,[bp+14]
        cmp     bx,[cs:lastdraw]
        jne     .rec
        cmp     cx,[cs:lastdraw+2]
        je      .out
.rec:   mov     [cs:lastdraw],bx
        mov     [cs:lastdraw+2],cx
        xor     dx,dx
        mov     [cs:rp4],dx
        xor     si,si
        mov     al,R_DLGDRAW
        call    ring_put
.out:   pop     dx
        pop     cx
        pop     bx
        jmp     prologue_exit

; --- 5: evGetEvent  (m107C:0164) : poll counter + travel hold ----------
h_evget:
        push    bx
        mov     ax,[bp+4]
        mov     bx,SEG_EVT
        call    learn_base
        pop     bx
        inc     word [cs:mb_polls]
        cmp     word [cs:mb_hold],0
        je      .go
        mov     word [cs:mb_holding],1
        sti
.wait:  hlt
        cmp     word [cs:mb_hold],0
        jne     .wait
        cli
        mov     word [cs:mb_holding],0
.go:    jmp     prologue_exit

; --- 6: game started with state struct (o15:00E0) ----------------------
h_state:
        push    bx
        push    cx
        push    dx
        mov     bx,[bp+12]
        mov     cx,[bp+14]
        mov     [cs:mb_state],bx
        mov     [cs:mb_state+2],cx
        xor     dx,dx
        mov     [cs:rp4],dx
        xor     si,si
        mov     al,R_STATE
        call    ring_put
        pop     dx
        pop     cx
        pop     bx
        jmp     prologue_exit

; --- 7: hunting sprite update (o6:1949) --------------------------------
h_hunt:
        push    bx
        push    cx
        push    dx
        mov     bx,[bp+12]
        mov     cx,[bp+14]
        cmp     bx,[cs:mb_hunt]
        jne     .new
        cmp     cx,[cs:mb_hunt+2]
        je      .same
.new:   mov     [cs:mb_hunt],bx
        mov     [cs:mb_hunt+2],cx
        xor     dx,dx
        mov     [cs:rp4],dx
        xor     si,si
        mov     al,R_HUNT
        call    ring_put
.same:  pop     dx
        pop     cx
        pop     bx
        jmp     prologue_exit

; --- 8: rafting update (o10:057F) --------------------------------------
h_raft:
        push    bx
        push    cx
        push    dx
        mov     bx,[bp+12]
        mov     cx,[bp+14]
        cmp     bx,[cs:mb_raft]
        jne     .new
        cmp     cx,[cs:mb_raft+2]
        je      .same
.new:   mov     [cs:mb_raft],bx
        mov     [cs:mb_raft+2],cx
        xor     dx,dx
        mov     [cs:rp4],dx
        xor     si,si
        mov     al,R_RAFT
        call    ring_put
.same:  pop     dx
        pop     cx
        pop     bx
        jmp     prologue_exit

; ---------------------------------------------------------------------
; INT 1Ch : deliver queued input, one command per tick
; ---------------------------------------------------------------------
int1c:
        pushf
        call    far [cs:old1c]
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es
        push    cs
        pop     ds
        inc     word [mb_heart]
        cmp     word [mb_relpend],0
        je      .noqueuedrel
        mov     word [mb_relpend],0
        mov     ax,4                    ; left released
        xor     bl,bl
        mov     cx,[mb_relx]
        mov     dx,[mb_rely]
        call    post_mouse
        jmp     .out
.noqueuedrel:
        mov     si,[mb_inqt]
        cmp     si,[mb_inqh]
        je      .out
        mov     bx,si
        and     bx,INQSIZE-1
        mov     cl,3
        shl     bx,cl
        add     bx,mb_inq
        inc     si
        mov     [mb_inqt],si
        mov     ax,[bx]                 ; type
        mov     cx,[bx+2]               ; a
        mov     dx,[bx+4]               ; b
        cmp     ax,I_KEY
        je      .key
        cmp     ax,I_CLICK
        je      .click
        cmp     ax,I_MOVE
        je      .move
        cmp     ax,I_PRESS
        je      .press
        cmp     ax,I_RELEASE
        je      .release
        jmp     .out
.key:   call    bios_key
        jmp     .out
.click: mov     [mb_relx],cx
        mov     [mb_rely],dx
        mov     word [mb_relpend],1
.press: mov     ax,2                    ; left pressed
        mov     bl,1
        call    post_mouse
        jmp     .out
.release:
        mov     ax,4
        xor     bl,bl
        call    post_mouse
        jmp     .out
.move:  call    set_cursor
.out:   pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        iret

; set the game's cursor position (CX = x, DX = y)
set_cursor:
        mov     ax,[mb_dgroup]
        or      ax,ax
        jz      .no
        mov     es,ax
        mov     [es:DG_CURX],cx
        mov     [es:DG_CURY],dx
.no:    ret

; post a mouse event into the game's queue
;   AX = event code (2 press / 4 release), BL = buttons, CX = x, DX = y
post_mouse:
        push    ax
        mov     ax,[mb_dgroup]
        or      ax,ax
        jz      .drop
        mov     es,ax
        cmp     word [es:DG_EVQMAX],0
        je      .drop
        call    set_cursor
        pop     ax
        mov     [evbuf+0],ax
        mov     word [evbuf+2],0
        push    es
        mov     ax,40h
        mov     es,ax
        mov     al,[es:17h]
        pop     es
        mov     [evbuf+4],al
        mov     [evbuf+5],bl
        mov     [evbuf+6],cx
        mov     [evbuf+8],dx
        mov     word [evbuf+10],0
        mov     word [evbuf+12],0
        ; evPostEvent(ev far*)  - pascal, callee pops 4 bytes
        push    cs
        mov     ax,evbuf
        push    ax
        mov     ax,[mb_cs_evt]
        mov     [evpost+2],ax
        call    far [evpost]
        ret
.drop:  pop     ax
        inc     word [mb_dropped]
        ret
evpost  dw      EVPOST_OFF, 0

; insert a key into the BIOS keyboard buffer (CX = key word)
bios_key:
        mov     ax,40h
        mov     es,ax
        cli
        mov     bx,[es:1Ch]             ; tail
        mov     si,bx
        add     si,2
        cmp     si,[es:82h]             ; buffer end
        jb      .nw
        mov     si,[es:80h]             ; buffer start
.nw:    cmp     si,[es:1Ah]             ; full?
        je      .full
        mov     [es:bx],cx
        mov     [es:1Ch],si
        sti
        ret
.full:  sti
        inc     word [mb_dropped]
        ret

resident_end:

; =====================================================================
; Installation (discarded after going resident)
; =====================================================================
install:
        ; already installed?  look for our signature behind the hook vector
        mov     ax,3500h+HOOKINT
        int     21h
        mov     ax,es
        or      ax,ax
        jz      .fresh
        mov     di,mbox
        mov     si,mbox
        mov     cx,8
        repe    cmpsw
        jne     .fresh
        mov     dx,msg_already
        mov     ah,9
        int     21h
        mov     ax,4C01h
        int     21h
.fresh:
        push    cs
        pop     es
        ; self linear address
        mov     ax,cs
        mov     dx,16
        mul     dx                      ; DX:AX = cs*16
        add     ax,mbox
        adc     dx,0
        mov     [mb_self],ax
        mov     [mb_self+2],dx
        ; hook vectors
        mov     ax,351Ch
        int     21h
        mov     [old1c],bx
        mov     [old1c+2],es
        mov     ax,3500h+HOOKINT
        int     21h
        mov     [old66],bx
        mov     [old66+2],es
        mov     dx,int1c
        mov     ax,251Ch
        int     21h
        mov     dx,int66
        mov     ax,2500h+HOOKINT
        int     21h
        ; free our environment block
        mov     es,[2Ch]
        mov     ah,49h
        int     21h
        mov     dx,msg_ok
        mov     ah,9
        int     21h
        ; go resident
        mov     dx,resident_end+15
        mov     cl,4
        shr     dx,cl
        mov     ax,3100h
        int     21h

msg_ok          db      'Oregon Trail Accessibility helper loaded.',13,10,'$'
msg_already     db      'Oregon Trail Accessibility helper is already loaded.',13,10,'$'
