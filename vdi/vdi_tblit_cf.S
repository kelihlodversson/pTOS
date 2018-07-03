
|########################################################################
|                                                                       #
|       "vdi_tblit_cf.S" - Translated from                              #
|               file "vdi_tblit_preprocessed.s"                         #
|                                                                       #
|-----------------------------------------------------------------------#
|                                                                       #
|       PortAsm Code Translator Copyright (c) MicroAPL 1990-2004        #
|                   All Rights Reserved Worldwide                       #
|                                                                       #
|########################################################################
| Translated on Mon Jul  2 20:31:44 2018 by version 2.6.0 of PortAsm translator
| PortAsm Licence Number #3250
| Target assembler:    Gnu 'gas'
|
| Non-default options specified:
|       -a gnu              - Specify target assembler
|       -blanks on          - Allow/Disallow blanks in source operand
|       -core v4            - Specify processor core version number
|       -hardware_divide on
|                           - Target CPU has hardware-divide unit
|       -hardware_mac on    - Target CPU has multiply-accumulate unit
|       -noerrfile on       - Suppress .err file, errors to STDERR only
|       -nowarning 402,502,900,1111,1150
|                           - Suppress specified warnings
|       -o vdi_tblit_cf.S   - Filename for output file
|       -out_syntax standard
|                           - Specify output instruction syntax
|

|##################################################
| Additional equates for translation              #
|##################################################
	.text

|########################################################################
| Start of translated code                                              #
|########################################################################
| 1 "vdi/vdi_tblit.S"
| 1 "<built-in>"
| 1 "<command-line>"
| 1 "vdi/vdi_tblit.S"
| 14 "vdi/vdi_tblit.S"
| 1 "include/asmdefs.h" 1
| 23 "include/asmdefs.h"
| 1 "include/config.h" 1
| 25 "include/config.h"
| 1 "include/../localconf.h" 1
| 26 "include/config.h" 2
| 24 "include/asmdefs.h" 2
| 15 "vdi/vdi_tblit.S" 2
| 1 "vdi/vdi_asm.h" 1
| 16 "vdi/vdi_tblit.S" 2
| 1 "vdi/lineaasm_m68k.h" 1
| 17 "vdi/vdi_tblit.S" 2
| 115 "vdi/vdi_tblit.S"
|;;           .globl _normal_blit      | [19]
	.globl	_normal_blit
|;;           .globl _outline          | [20]
	.globl	_outline
|;;           .globl _rotate           | [21]
	.globl	_rotate
|;;           .globl _scale            | [22]
	.globl	_scale
| 129 "vdi/vdi_tblit.S"
|;;           .extern _linea_vars      | [24]
	.extern	_linea_vars
| 141 "vdi/vdi_tblit.S"
|;;           .globl _deftxbuf         | [26]
	.globl	_deftxbuf
|;;           .globl _scrtsiz          | [27]
	.globl	_scrtsiz
| 277 "vdi/vdi_tblit.S"
|;;           .bss                     | [29]
	.p2align 2,0
	.bss
_deftxbuf:
|;;           .ds.b ((2 +6 +8 +15)/16)*2*16*2 +(((2*(2 +6 +8))+3+15)/16)*2*(2*16)+2 +(((2*(2 +6 +8))+3+15)/16)*2 +2 | [31]
	.space	(2+6+8+15)/16*2*16*2+(2*(2+6+8)+3+15)/16*2*(2*16)+2+(2*(2+6+8)+3+15)/16*2+2

|;;           .text                    | [33]
	.p2align 2,0
	.text

|;;           .even                    | [35]
	.p2align 1,0
_scrtsiz:
|;;           .dc.w ((2 +6 +8 +15)/16)*2*16*2 | [37]
	.short	(2+6+8+15)/16*2*16*2
| 392 "vdi/vdi_tblit.S"
|;;           .text                    | [39]






	.p2align 1,0
_normal_blit:
|;;   movem.l   d2-d7/a2-a6,-(sp)      | [47]
	lea      -44(%a7),%a7
	movem.l  %d2-%d7/%a2-%a6,(%a7)
      movea.l  (44+4,%a7),%a6          | [48]
      movea.l  (44+8,%a7),%a0          | [49]
      movea.l  (44+12,%a7),%a1         | [50]
      jbsr     norm_blt                | [51]
|;;   movem.l   (sp)+,d2-d7/a2-a6      | [52]
	movem.l  (%a7),%d2-%d7/%a2-%a6
	lea      44(%a7),%a7
      rts                              | [53]


_outline:
|;;   movem.l   d2-d7/a2-a6,-(sp)      | [57]
	lea      -44(%a7),%a7
	movem.l  %d2-%d7/%a2-%a6,(%a7)
      movea.l  (44+4,%a7),%a6          | [58]
      movea.l  (44+8,%a7),%a0          | [59]
      move.w   (44+12,%a7),%d7         | [60]
      jbsr     outlin                  | [61]
|;;   movem.l   (sp)+,d2-d7/a2-a6      | [62]
	movem.l  (%a7),%d2-%d7/%a2-%a6
	lea      44(%a7),%a7
      rts                              | [63]


_rotate:
|;;   movem.l   d2-d7/a2-a6,-(sp)      | [67]
	lea      -44(%a7),%a7
	movem.l  %d2-%d7/%a2-%a6,(%a7)
      movea.l  (44+4,%a7),%a6          | [68]
      jbsr     rotation                | [69]
|;;   movem.l   (sp)+,d2-d7/a2-a6      | [70]
	movem.l  (%a7),%d2-%d7/%a2-%a6
	lea      44(%a7),%a7
      rts                              | [71]


_scale:
|;;   movem.l   d2-d7/a2-a6,-(sp)      | [75]
	lea      -44(%a7),%a7
	movem.l  %d2-%d7/%a2-%a6,(%a7)
      movea.l  (44+4,%a7),%a6          | [76]
      jbsr     replicat                | [77]
|;;   movem.l   (sp)+,d2-d7/a2-a6      | [78]
	movem.l  (%a7),%d2-%d7/%a2-%a6
	lea      44(%a7),%a7
      rts                              | [79]


|;;           .text                    | [82]
| 444 "vdi/vdi_tblit.S"
|;;                   .even            | [84]
	.p2align 1,0
|;;   lf_tab: .dc.w 0xFFFF             | [85]
lf_tab:
	.short	0xFFFF
|;;   rf_tab: .dc.w 0x7FFF             | [86]
rf_tab:
	.short	0x7FFF
|;;                   .dc.w 0x3FFF     | [87]
	.short	0x3FFF
|;;                   .dc.w 0x1FFF     | [88]
	.short	0x1FFF
|;;                   .dc.w 0x0FFF     | [89]
	.short	0x0FFF
|;;                   .dc.w 0x07FF     | [90]
	.short	0x07FF
|;;                   .dc.w 0x03FF     | [91]
	.short	0x03FF
|;;                   .dc.w 0x01FF     | [92]
	.short	0x01FF
|;;                   .dc.w 0x00FF     | [93]
	.short	0x00FF
|;;                   .dc.w 0x007F     | [94]
	.short	0x007F
|;;                   .dc.w 0x003F     | [95]
	.short	0x003F
|;;                   .dc.w 0x001F     | [96]
	.short	0x001F
|;;                   .dc.w 0x000F     | [97]
	.short	0x000F
|;;                   .dc.w 0x0007     | [98]
	.short	0x0007
|;;                   .dc.w 0x0003     | [99]
	.short	0x0003
|;;                   .dc.w 0x0001     | [100]
	.short	0x0001
|;;                   .dc.w 0x0000     | [101]
	.short	0x0000
| 488 "vdi/vdi_tblit.S"
wrmappin:
|;;           .dc.b 4*0,4*0,4*3,4*3    | [104]
	.byte	4*0,4*0,4*3,4*3
|;;           .dc.b 4*4,4*4,4*7,4*7    | [105]
	.byte	4*4,4*4,4*7,4*7
|;;           .dc.b 4*6,4*6,4*6,4*6    | [106]
	.byte	4*6,4*6,4*6,4*6
|;;           .dc.b 4*1,4*1,4*13,4*13  | [107]
	.byte	4*1,4*1,4*13,4*13

|;;           .dc.b 4*0,4*15,4*0,4*15  | [109]
	.byte	4*0,4*15,4*0,4*15
|;;           .dc.b 4*0,4*14,4*1,4*15  | [110]
	.byte	4*0,4*14,4*1,4*15
|;;           .dc.b 4*0,4*13,4*2,4*15  | [111]
	.byte	4*0,4*13,4*2,4*15
|;;           .dc.b 4*0,4*12,4*3,4*15  | [112]
	.byte	4*0,4*12,4*3,4*15
|;;           .dc.b 4*0,4*11,4*4,4*15  | [113]
	.byte	4*0,4*11,4*4,4*15
|;;           .dc.b 4*0,4*10,4*5,4*15  | [114]
	.byte	4*0,4*10,4*5,4*15
|;;           .dc.b 4*0,4*9,4*6,4*15   | [115]
	.byte	4*0,4*9,4*6,4*15
|;;           .dc.b 4*0,4*8,4*7,4*15   | [116]
	.byte	4*0,4*8,4*7,4*15
|;;           .dc.b 4*0,4*7,4*8,4*15   | [117]
	.byte	4*0,4*7,4*8,4*15
|;;           .dc.b 4*0,4*6,4*9,4*15   | [118]
	.byte	4*0,4*6,4*9,4*15
|;;           .dc.b 4*0,4*5,4*10,4*15  | [119]
	.byte	4*0,4*5,4*10,4*15
|;;           .dc.b 4*0,4*4,4*11,4*15  | [120]
	.byte	4*0,4*4,4*11,4*15
|;;           .dc.b 4*0,4*3,4*12,4*15  | [121]
	.byte	4*0,4*3,4*12,4*15
|;;           .dc.b 4*0,4*2,4*13,4*15  | [122]
	.byte	4*0,4*2,4*13,4*15
|;;           .dc.b 4*0,4*1,4*14,4*15  | [123]
	.byte	4*0,4*1,4*14,4*15
|;;           .dc.b 4*0,4*0,4*15,4*15  | [124]
	.byte	4*0,4*0,4*15,4*15

toptable:
|;;           .dc.l top0,top1,top2,top3,top4,top5,top6,top7 | [127]
	.long	top0,top1,top2,top3,top4,top5,top6,top7
|;;           .dc.l top8,top9,topa,topb,topc,topd,tope,topf | [128]
	.long	top8,top9,topa,topb,topc,topd,tope,topf

twoptble:
|;;           .dc.l twop0,twop1,twop2,twop3,twop4,twop5,twop6,twop7 | [131]
	.long	twop0,twop1,twop2,twop3,twop4,twop5,twop6,twop7
|;;           .dc.l twop8,twop9,twopa,twopb,twopc,twopd,twope,twopf | [132]
	.long	twop8,twop9,twopa,twopb,twopc,twopd,twope,twopf

|;;           .text                    | [134]
| 532 "vdi/vdi_tblit.S"
	.p2align 1,0
norm_blt:
|***	SAVE	d5	***
	lea      -4(%a7),%a7
	movem.l  %d5,(%a7)
      move.w   (-26,%a6),%d1           | [137]
|;;   sub.w     -24(a6),d1             | [138]
	move.w   %d1,%d5
	move.w   (-24,%a6),%d3
	sub.l    %d3,%d5
	move.w   %d5,%d1
      move.w   %d1,%d0                 | [139 PL]
      jpl      do_rot                  | [140]



|;;   neg.w     d1                     | [144]
	neg.l    %d5
|;;   ori.w     #0x8000,d1             | [145]
	move.w   %d5,%d1
	ori.l    #(0xFFFF&0x8000),%d1
|;;   addi.w    #16,d0                 | [146]
	move.w   %d0,%d5
	addi.l   #16,%d5
	move.w   %d5,%d0
do_rot:
      move.w   %d1,(-42,%a6)           | [148]

|;;   add.w     d0,d0                  | [150]
	move.w   %d0,%d5
	add.l    %d0,%d5
      lea      lf_tab,%a2              | [151]
	move.w   %d5,%d0
|;;   move.w    (a2,d0.w),d0           | [152]
	move.w   %d0,%a5
	move.w   (0,%a2,%a5.l*1),%d0
|;;   not.w     d0                     | [153]
	move.w   %d0,%d5
	not.l    %d5
|;;   move.w    d0,-40(a6)             | [154]
	move.w   %d5,(-40,%a6)



get_mask:
      move.w   (-26,%a6),%d0           | [159]
|;;   add.w     d0,d0                  | [160]
	move.w   %d0,%d5
	add.l    %d0,%d5
	move.w   %d5,%d0
|;;   move.w    (a2,d0.w),-38(a6)      | [161]
	move.w   %d0,%a5
	move.w   (0,%a2,%a5.l*1),%d3
	move.w   %d3,(-38,%a6)

|;;   lsr.w     #1,d0                  | [163]
	and.l    #0xffff,%d5
	lsr.l    #1,%d5
|;;   add.w     -20(a6),d0             | [164]
	move.w   (-20,%a6),%d3
	add.l    %d3,%d5


|;;   move.w    d0,d2                  | [167]
	move.w   %d5,%d2
|;;   sub.w     -74(a6),d2             | [168]
	move.w   %d5,%d0
	move.w   %d2,%d5
	move.w   (-74,%a6),%d3
	sub.l    %d3,%d5
|;;   andi.w    #0xF,d2                | [169]
	andi.l   #(0xFFFF&0xF),%d5
|;;   add.w     d2,d2                  | [170]
	add.l    %d5,%d5
	move.w   %d5,%d2
|;;   move.w    (a2,d2.w),d2           | [171]
	move.w   %d2,%a5
	move.w   (0,%a2,%a5.l*1),%d2
|;;   not.w     d2                     | [172]
	move.w   %d2,%d5
	not.l    %d5
|;;   move.w    d2,-34(a6)             | [173]
	move.w   %d5,(-34,%a6)
      clr.w    %d4                     | [174]
      move.w   #0x8000,(-80,%a6)       | [175]
      moveq.l  #(-1),%d3               | [176]
	move.w   %d5,%d2


      cmp.w    #0x10,%d0               | [179 HI]
      jhi      doub_des                | [180]



|;;   add.w     d0,d0                  | [184]
	move.w   %d0,%d5
	add.l    %d0,%d5
	move.w   %d5,%d0
|;;   move.w    (a2,d0.w),d1           | [185]
	move.w   %d0,%a5
	move.w   (0,%a2,%a5.l*1),%d1
|;;   not.w     d1                     | [186]
	move.w   %d1,%d5
	not.l    %d5
|;;   and.w     d1,-38(a6)             | [187]
	move.w   (-38,%a6),%d7
	and.l    %d5,%d7
	move.w   %d7,(-38,%a6)

      moveq.l  #(-4),%d7               | [189]
	move.w   %d5,%d1
      jra      msk_done                | [190]



doub_des:
      move.w   %d0,%d1                 | [195]
|;;   lsr.w     #4,d1                  | [196]
	mvz.w    %d1,%d5
	lsr.l    #4,%d5
|;;   subq.w    #1,d1                  | [197]
	subq.l   #1,%d5
	move.w   %d5,%d1
      move.w   %d1,%d3                 | [198 NE]
      jne      mlt_dest                | [199]



      move.w   (-24,%a6),%d1           | [203]
|;;   add.w     -20(a6),d1             | [204]
	move.w   %d1,%d5
	move.w   (-20,%a6),%d7
	add.l    %d7,%d5
	move.w   %d5,%d1
      cmp.w    #0x20,%d1               | [205 CC]
      jcc      mlt_dest                | [206]
      moveq.l  #0,%d7                  | [207]
      jra      do_ritem                | [208]

mlt_dest:
      tst.w    (-42,%a6)               | [211 LT]
      jlt      mltleft                 | [212]
      moveq.l  #4,%d7                  | [213]
      jra      do_ritem                | [214]
mltleft:
      moveq.l  #8,%d7                  | [216]
do_ritem:
|;;   andi.w    #0x000F,d0             | [218 NE]
	andi.l   #(0xFFFF0000!~0x000F),%d0
	tst.w    %d0
      jne      not_null                | [219]
|;;   subq.w    #1,d3                  | [220]
	move.w   %d3,%d5
	subq.l   #1,%d5
      moveq.l  #0x10,%d0               | [221]
	move.w   %d5,%d3
not_null:
|;;   add.w     d0,d0                  | [223]
	move.w   %d0,%d5
	add.l    %d0,%d5
	move.w   %d5,%d0
|;;   move.w    (a2,d0.w),d4           | [224]
	move.w   %d0,%a5
	move.w   (0,%a2,%a5.l*1),%d4
|;;   not.w     d4                     | [225]
	move.w   %d4,%d5
	not.l    %d5
	move.w   %d5,%d4

msk_done:
      move.w   %d7,(-124,%a6)          | [228]
      move.w   %d3,(-28,%a6)           | [229]
      move.w   %d4,(-36,%a6)           | [230]
|;;   addq.w    #2,d3                  | [231]
	move.w   %d3,%d5
	addq.l   #2,%d5
	move.w   %d5,%d3
      cmp.w    %d2,%d4                 | [232 CS]
      jcs      msk0                    | [233]
|;;   addq.w    #1,d3                  | [234]
	addq.l   #1,%d5
	move.w   %d5,%d3
msk0:
      move.w   %d3,(-32,%a6)           | [236]
      move.w   %d3,(-30,%a6)           | [237]
|***	RESTORE	AFTER d5	***
	move.l   (%a7)+,%d5

plane_loop:
|***	SAVE	d1-d2/a0	***
	lea      -12(%a7),%a7
	movem.l  %d1-%d2/%a0,(%a7)
      move.w   (-98,%a6),%d0           | [240]
|;;   lsr.w     -84(a6)                | [241 X]
	mvz.w    (-84,%a6),%d1
	lsr.l    #1,%d1
	move.w   %d1,(-84,%a6)
|;;   addx.w    d0,d0                  | [242]
	move.w   %d0,%d1
	addx.l   %d0,%d1
|;;   lsr.w     -76(a6)                | [243 X]
	mvz.w    (-76,%a6),%d2
	lsr.l    #1,%d2
	move.w   %d2,(-76,%a6)
|;;   addx.w    d0,d0                  | [244]
	addx.l   %d1,%d1



      lea      toptable,%a4            | [248]
	move.w   %d1,%d0
|;;   move.b    wrmappin-toptable(a4,d0.w),d0 | [249]
	move.w   %d0,%a5
	move.b   (wrmappin-toptable,%a4,%a5.l*1),%d0
|;;   movea.l   toptable-toptable(a4,d0.w),a2 | [250]
	move.w   %d0,%a5
	movea.l  (toptable-toptable,%a4,%a5.l*1),%a2
      movea.l  %a2,%a3                 | [251]
      movea.l  %a2,%a5                 | [252]

|;;   movea.l   twoptble-toptable(a4,d0.w),a4 | [254]
	move.w   %d0,%a0
	movea.l  (twoptble-toptable,%a4,%a0.l*1),%a4



      move.w   (-96,%a6),%d0           | [258 EQ]
      jeq      wrt_char                | [259]

      btst     #1,%d0                  | [261 EQ]
      jeq      no_lite                 | [262]



|;;   move.w    _linea_vars+1002,-78(a6) | [266]
	move.w   _linea_vars+1002,%d1
	move.w   %d1,(-78,%a6)
      move.l   %a5,(-52,%a6)           | [267]
      lea      liteop,%a5              | [268]

      move.l   %a2,(-64,%a6)           | [270]
      lea      liteopwf,%a2            | [271]

      move.l   %a4,(-72,%a6)           | [273]
      lea      liteopw,%a4             | [274]

no_lite:
      btst     #0,%d0                  | [277 EQ]
      jeq      no_thick                | [278]



      clr.w    (-82,%a6)               | [282]

      move.l   %a5,(-48,%a6)           | [284]
      lea      thknop,%a5              | [285]

      move.l   %a2,(-60,%a6)           | [287]
      lea      thknopwf,%a2            | [288]

      move.l   %a4,(-68,%a6)           | [290]
      lea      thknopw,%a4             | [291]

no_thick:
      btst     #2,%d0                  | [294 EQ]
      jeq      wrt_char                | [295]



|;;   move.w    _linea_vars+1004,-80(a6) | [299]
	move.w   _linea_vars+1004,%d1
	move.w   %d1,(-80,%a6)
      move.l   %a5,(-56,%a6)           | [300]
      lea      skewop,%a5              | [301]

      tst.w    (-124,%a6)              | [303 EQ GT]
      jgt      wrt_char                | [304]
      jeq      not_sngl                | [305]
      clr.w    (-28,%a6)               | [306]
      moveq.l  #0,%d7                  | [307]
      jra      chng_blt                | [308]
not_sngl:
|;;   cmpi.w    #0x10,-20(a6)          | [310 LS]
	move.w   (-20,%a6),%d1
	cmp.w    #0x10,%d1
      jls      wrt_char                | [311]

      tst.w    (-42,%a6)               | [313 LT]
      jlt      chng_left               | [314]
      moveq.l  #4,%d7                  | [315]
      jra      chng_blt                | [316]
chng_left:
      moveq.l  #8,%d7                  | [318]
chng_blt:
      move.w   %d7,(-124,%a6)          | [320]

wrt_char:
      move.w   (-124,%a6),%d7          | [323 LE]
      jle      set_chr_hgt             | [324]
      movea.l  %a2,%a5                 | [325]
      move.w   (-18,%a6),%d5           | [326]
set_chr_hgt:
      move.w   (-22,%a6),%d3           | [328]
|;;   subq.w    #1,d3                  | [329]
	move.w   %d3,%d1
	subq.l   #1,%d1

|;;   movea.l   blttype+4(pc,d7.w),a2  | [331]
	move.w   %d7,%a2
	movea.l  #blttype+4,%a0
	movea.l  (0,%a0,%a2.l*1),%a2
	move.w   %d1,%d3
|***	RESTORE	AFTER d1-d2/a0	***
	movem.l  (%a7),%d1-%d2/%a0
	lea      12(%a7),%a7
      jmp      (%a2)                   | [332]
next_plane:
|***	SAVE	d0-d1	***
	lea      -8(%a7),%a7
	movem.l  %d0-%d1,(%a7)
|;;   subq.w    #1,-86(a6)             | [334 LE]
	mvs.w    (-86,%a6),%d0
	subq.l   #1,%d0
	move     %ccr,%d1
	move.w   %d0,(-86,%a6)
	move     %d1,%ccr
      jle      no_more_planes          | [335]
      movea.l  (-10,%a6),%a0           | [336]
      movea.l  (-14,%a6),%a1           | [337]
      addq.l   #2,%a1                  | [338]
      move.l   %a1,(-14,%a6)           | [339]
|***	RESTORE	d0-d1	***
	movem.l  (%a7),%d0-%d1
	lea      8(%a7),%a7
      btst     #2,(-96+1,%a6)          | [340 EQ]
      jeq      plane_loop              | [341]
      jra      norm_blt                | [342]
no_more_planes:
|***	RESTORE	d0-d1	***
	movem.l  (%a7),%d0-%d1
	lea      8(%a7),%a7
      rts                              | [344]
blttype:
|;;           .dc.l sgl_loop, dbl_loop, mlt_rite, mlt_left | [346]
	.long	sgl_loop,dbl_loop,mlt_rite,mlt_left




	.p2align 1,0
sgl_loop:
      move.w   (-38,%a6),%d2           | [352]
      move.w   (-16,%a6),%d6           | [353]
      move.w   (-18,%a6),%d7           | [354]
      lea      sgl_rtn,%a3             | [355]
      jra      sgl_1st_scan            | [356]

sgl_lp:
|;;   adda.w    d6,a0                  | [359]
	mvs.w    %d6,%d0
	adda.l   %d0,%a0
|;;   adda.w    d7,a1                  | [360]
	mvs.w    %d7,%d0
	adda.l   %d0,%a1
sgl_1st_scan:
      move.w   (%a1),%d4               | [362]
| 767 "vdi/vdi_tblit.S"
      move.l   (%a0),%d0               | [364]


      move.w   (-42,%a6),%d1           | [367 MI]
      jmi      left_rol                | [368]
      lsr.l    %d1,%d0                 | [369]
      jra      end_ro                  | [370]
left_rol:
      lsl.l    %d1,%d0                 | [372]
end_ro:
      swap     %d0                     | [374]
      move.w   %d0,%d1                 | [375]
      swap     %d0                     | [376]

      jmp      (%a5)                   | [378]

sgl_rtn:
      move.w   %d1,(%a1)               | [381]
|;;   dbra      d3,sgl_lp              | [382]
	tst.w    %d3
	jeq      _PA_381_
	subq.l   #1,%d3
	jra      sgl_lp
	_PA_381_:         
      jra      next_plane              | [383]




dbl_loop:
      movea.l  %a3,%a4                 | [389]
      move.w   (-16,%a6),%d6           | [390]
      move.w   (-18,%a6),%d7           | [391]
      lea      dbl_2wrd,%a2            | [392]
      move.l   %a2,%d5                 | [393]
      lea      dbl_1wrd,%a3            | [394]
      jra      dbl_1st_scan            | [395]

dbl_lp:
|;;   adda.w    d6,a0                  | [398]
	mvs.w    %d6,%d0
	adda.l   %d0,%a0
|;;   adda.w    d7,a1                  | [399]
	mvs.w    %d7,%d0
	adda.l   %d0,%a1
|;;   exg       a3,d5                  | [400]
	move.l   %a3,%d0
	move.l   %d5,%a3
	move.l   %d0,%d5
dbl_1st_scan:
      move.w   (%a1),%d4               | [402]
| 814 "vdi/vdi_tblit.S"
      move.l   (%a0),%d0               | [404]


      move.w   (-42,%a6),%d1           | [407 MI]
      jmi      dbleft                  | [408]
      lsr.l    %d1,%d0                 | [409]
      jra      dblendr                 | [410]
dbleft:
      lsl.l    %d1,%d0                 | [412]
dblendr:
      swap     %d0                     | [414]
      move.w   %d0,%d1                 | [415]
      swap     %d0                     | [416]
      move.w   (-38,%a6),%d2           | [417]

      jmp      (%a5)                   | [419]

dbl_1wrd:
|***	SAVE	d0/a0	***
	lea      -8(%a7),%a7
	movem.l  %d0/%a0,(%a7)
      move.w   %d1,(%a1)               | [422]
      move.w   %d0,%d1                 | [423]

      movea.w  (-88,%a6),%a2           | [425]
|;;   move.w    (a1,a2.w),d4           | [426]
	move.w   %a2,%a0
	move.w   (0,%a1,%a0.l*1),%d4

      move.w   (-36,%a6),%d2           | [428]

|;;   exg       a3,d5                  | [430]
	move.l   %a3,%d0
	move.l   %d5,%a3
	move.l   %d0,%d5
|***	RESTORE	AFTER d0/a0	***
	movem.l  (%a7),%d0/%a0
	lea      8(%a7),%a7
      jmp      (%a4)                   | [431]

dbl_2wrd:
|***	SAVE	a0	***
	lea      -4(%a7),%a7
	movem.l  %a0,(%a7)
      movea.w  (-88,%a6),%a2           | [434]
|;;   move.w    d1,(a1,a2.w)           | [435]
	move.w   %a2,%a0
	move.w   %d1,(0,%a1,%a0.l*1)
|***	RESTORE	AFTER a0	***
	move.l   (%a7)+,%a0

|;;   dbra      d3,dbl_lp              | [437]
	tst.w    %d3
	jeq      _PA_436_
	subq.l   #1,%d3
	jra      dbl_lp
	_PA_436_:         
      jra      next_plane              | [438]





left_loop:
      movea.l  %d6,%a0                 | [445]
      movea.l  %d7,%a1                 | [446]
|;;   adda.w    -16(a6),a0             | [447]
	mvs.w    (-16,%a6),%d6
	adda.l   %d6,%a0
|;;   adda.w    d5,a1                  | [448]
	mvs.w    %d5,%d6
	adda.l   %d6,%a1
      clr.w    (-82,%a6)               | [449]
|;;   rol.w     -78(a6)                | [450]
	clr.l    %d7
	move.w   (-78,%a6),%d6
	move.w   %d6,%d7
	lsl.l    #1,%d6
	lsr.l    #8,%d7
	lsr.l    #7,%d7
	or.l     %d7,%d6
	move.w   %d6,(-78,%a6)

      btst     #2,(-96+1,%a6)          | [452 NE]
      jne      skewopw                 | [453]
mlt_left:
      move.l   %a0,%d6                 | [455]
      move.l   %a1,%d7                 | [456]
      move.w   (-28,%a6),(-44,%a6)     | [457]







      move.l   (%a0)+,%d0              | [465]


      move.w   (%a1),%d4               | [468]

      move.w   (-42,%a6),%d1           | [470]
      lsl.l    %d1,%d0                 | [471]

      swap     %d0                     | [473]
      move.w   %d0,%d1                 | [474]
      swap     %d0                     | [475]

      move.w   (-38,%a6),%d2           | [477]

      lea      lft_lfrng,%a3           | [479]
      jmp      (%a5)                   | [480]

lft_lfrng:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      move.w   %d1,(%a1)               | [483]
|;;   adda.w    -88(a6),a1             | [484]
	mvs.w    (-88,%a6),%d0
	adda.l   %d0,%a1

      move.w   (-40,%a6),%d2           | [486]
      lea      lft_wrds,%a3            | [487]
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0



word_lef:
|***	SAVE	d3/d5	***
	lea      -8(%a7),%a7
	movem.l  %d3/%d5,(%a7)
      move.w   %d0,%d4                 | [492]
|;;   and.w     d2,d4                  | [493]
	move.w   %d4,%d3
	and.l    %d2,%d3
      move.w   (%a0)+,%d0              | [494]





      swap     %d0                     | [500]
      move.w   %d1,%d0                 | [501]
      swap     %d0                     | [502]

      move.w   (-42,%a6),%d1           | [504]
      lsl.l    %d1,%d0                 | [505]

      swap     %d0                     | [507]
      move.w   %d0,%d1                 | [508]
      swap     %d0                     | [509]

|;;   not.w     d2                     | [511]
	move.w   %d2,%d5
	not.l    %d5
|;;   and.w     d2,d1                  | [512]
	move.w   %d3,%d4
	move.w   %d1,%d3
	and.l    %d5,%d3
|;;   not.w     d2                     | [513]
	not.l    %d5
|;;   eor.w     d4,d1                  | [514]
	eor.l    %d4,%d3

      move.w   (%a1),%d4               | [516]
	move.w   %d3,%d1
	move.w   %d5,%d2

|;;   subq.w    #1,-44(a6)             | [518 LT]
	mvs.w    (-44,%a6),%d3
	subq.l   #1,%d3
	move     %ccr,%d5
	move.w   %d3,(-44,%a6)
	move     %d5,%ccr
|***	RESTORE	AFTER d3/d5	***
	movem.l  (%a7),%d3/%d5
	lea      8(%a7),%a7
      jlt      lef_don                 | [519]

      jmp      (%a4)                   | [521]

lft_wrds:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      move.w   %d1,(%a1)               | [524]
|;;   adda.w    -88(a6),a1             | [525]
	mvs.w    (-88,%a6),%d0
	adda.l   %d0,%a1

|***	RESTORE	d0	***
	move.l   (%a7)+,%d0
      jra      word_lef                | [527]

lef_don:
      move.w   (-36,%a6),%d2           | [530]

      lea      lft_rfrng,%a3           | [532]
      jmp      (%a5)                   | [533]

lft_rfrng:
      move.w   %d1,(%a1)               | [536]

|;;   dbra      d3,left_loop           | [538]
	tst.w    %d3
	jeq      _PA_537_
	subq.l   #1,%d3
	jra      left_loop
	_PA_537_:         
      jra      next_plane              | [539]





rite_loop:
      movea.l  %d6,%a0                 | [546]
      movea.l  %d7,%a1                 | [547]
|;;   adda.w    -16(a6),a0             | [548]
	mvs.w    (-16,%a6),%d6
	adda.l   %d6,%a0
|;;   adda.w    d5,a1                  | [549]
	mvs.w    %d5,%d6
	adda.l   %d6,%a1
      clr.w    (-82,%a6)               | [550]
|;;   rol.w     -78(a6)                | [551]
	clr.l    %d7
	move.w   (-78,%a6),%d6
	move.w   %d6,%d7
	lsl.l    #1,%d6
	lsr.l    #8,%d7
	lsr.l    #7,%d7
	or.l     %d7,%d6
	move.w   %d6,(-78,%a6)

      btst     #2,(-96+1,%a6)          | [553 NE]
      jne      skewopw                 | [554]
mlt_rite:
      move.l   %a0,%d6                 | [556]
      move.l   %a1,%d7                 | [557]
      move.w   (-28,%a6),(-44,%a6)     | [558]
      move.w   (%a0)+,%d0              | [559]



      swap     %d0                     | [563]

      move.w   (%a1),%d4               | [565]

      move.w   (-42,%a6),%d1           | [567]
      lsr.l    %d1,%d0                 | [568]
      swap     %d0                     | [569]
      move.w   %d0,%d1                 | [570]
      swap     %d0                     | [571]

      move.w   (-38,%a6),%d2           | [573]

      lea      rgt_lfrng,%a3           | [575]
      jmp      (%a5)                   | [576]

rgt_lfrng:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      move.w   %d1,(%a1)               | [579]
|;;   adda.w    -88(a6),a1             | [580]
	mvs.w    (-88,%a6),%d0
	adda.l   %d0,%a1

      move.w   (-40,%a6),%d2           | [582]
      lea      rgt_wrds,%a3            | [583]
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0



word_rit:
|***	SAVE	d3/d5	***
	lea      -8(%a7),%a7
	movem.l  %d3/%d5,(%a7)
      move.w   %d0,%d4                 | [588]
|;;   and.w     d2,d4                  | [589]
	move.w   %d4,%d3
	and.l    %d2,%d3
      swap     %d0                     | [590]
      move.w   (%a0)+,%d0              | [591]



      swap     %d0                     | [595]

      move.w   (-42,%a6),%d1           | [597]
      lsr.l    %d1,%d0                 | [598]
      swap     %d0                     | [599]
      move.w   %d0,%d1                 | [600]
      swap     %d0                     | [601]

|;;   not.w     d2                     | [603]
	move.w   %d2,%d5
	not.l    %d5
|;;   and.w     d2,d1                  | [604]
	move.w   %d3,%d4
	move.w   %d1,%d3
	and.l    %d5,%d3
|;;   not.w     d2                     | [605]
	not.l    %d5
|;;   eor.w     d4,d1                  | [606]
	eor.l    %d4,%d3

      move.w   (%a1),%d4               | [608]
	move.w   %d3,%d1
	move.w   %d5,%d2

|;;   subq.w    #1,-44(a6)             | [610 LT]
	mvs.w    (-44,%a6),%d3
	subq.l   #1,%d3
	move     %ccr,%d5
	move.w   %d3,(-44,%a6)
	move     %d5,%ccr
|***	RESTORE	AFTER d3/d5	***
	movem.l  (%a7),%d3/%d5
	lea      8(%a7),%a7
      jlt      rite_don                | [611]

      jmp      (%a4)                   | [613]

rgt_wrds:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      move.w   %d1,(%a1)               | [616]
|;;   adda.w    -88(a6),a1             | [617]
	mvs.w    (-88,%a6),%d0
	adda.l   %d0,%a1

|***	RESTORE	d0	***
	move.l   (%a7)+,%d0
      jra      word_rit                | [619]

rite_don:
      move.w   (-36,%a6),%d2           | [622]

      lea      rgt_rfrng,%a3           | [624]
      jmp      (%a5)                   | [625]

rgt_rfrng:
      move.w   %d1,(%a1)               | [628]

|;;   dbra      d3,rite_loop           | [630]
	tst.w    %d3
	jeq      _PA_629_
	subq.l   #1,%d3
	jra      rite_loop
	_PA_629_:         
      jra      next_plane              | [631]
| 1056 "vdi/vdi_tblit.S"
top0:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d2                     | [634]
	move.w   %d2,%d0
	not.l    %d0
|;;   and.w     d2,d4                  | [635]
	move.w   %d0,%d2
	move.w   %d4,%d0
	and.l    %d2,%d0
|;;   not.w     d2                     | [636]
	move.w   %d0,%d4
	move.w   %d2,%d0
	not.l    %d0
      move.w   %d4,%d1                 | [637]
	move.w   %d0,%d2
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [638]

top1:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d2                     | [641]
	move.w   %d2,%d0
	not.l    %d0
|;;   or.w      d2,d1                  | [642]
	move.w   %d0,%d2
	and.l    #0xffff,%d0
	or.l     %d0,%d1
|;;   not.w     d2                     | [643]
	not.l    %d0
|;;   and.w     d4,d1                  | [644]
	move.w   %d0,%d2
	move.w   %d1,%d0
	and.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [645]

top2:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d1                     | [648]
	move.w   %d1,%d0
	not.l    %d0
|;;   and.w     d2,d1                  | [649]
	and.l    %d2,%d0
|;;   or.w      d4,d1                  | [650]
	or.l     %d4,%d0
|;;   eor.w     d2,d1                  | [651]
	eor.l    %d2,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [652]

top3:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   eor.w     d4,d1                  | [655]
	move.w   %d1,%d0
	eor.l    %d4,%d0
|;;   and.w     d2,d1                  | [656]
	and.l    %d2,%d0
|;;   eor.w     d4,d1                  | [657]
	eor.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [658]

top4:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   and.w     d2,d1                  | [661]
	move.w   %d1,%d0
	and.l    %d2,%d0
|;;   not.w     d1                     | [662]
	not.l    %d0
|;;   and.w     d4,d1                  | [663]
	and.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [664]

top5:
      move.w   %d4,%d1                 | [667]
      jmp      (%a3)                   | [668]

top6:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   and.w     d2,d1                  | [671]
	move.w   %d1,%d0
	and.l    %d2,%d0
|;;   eor.w     d4,d1                  | [672]
	eor.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [673]

top7:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   and.w     d2,d1                  | [676]
	move.w   %d1,%d0
	and.l    %d2,%d0
|;;   or.w      d4,d1                  | [677]
	or.l     %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [678]

top8:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   and.w     d2,d1                  | [681]
	move.w   %d1,%d0
	and.l    %d2,%d0
|;;   or.w      d4,d1                  | [682]
	or.l     %d4,%d0
|;;   eor.w     d2,d1                  | [683]
	eor.l    %d2,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [684]

top9:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   and.w     d2,d1                  | [687]
	move.w   %d1,%d0
	and.l    %d2,%d0
|;;   eor.w     d4,d1                  | [688]
	eor.l    %d4,%d0
|;;   eor.w     d2,d1                  | [689]
	eor.l    %d2,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [690]

topa:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      move.w   %d4,%d1                 | [693]
|;;   eor.w     d2,d1                  | [694]
	move.w   %d1,%d0
	eor.l    %d2,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [695]

topb:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   eor.w     d2,d4                  | [698]
	move.w   %d4,%d0
	eor.l    %d2,%d0
|;;   and.w     d2,d1                  | [699]
	move.w   %d0,%d4
	move.w   %d1,%d0
	and.l    %d2,%d0
|;;   or.w      d4,d1                  | [700]
	or.l     %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [701]

topc:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   eor.w     d4,d1                  | [704]
	move.w   %d1,%d0
	eor.l    %d4,%d0
|;;   and.w     d2,d1                  | [705]
	and.l    %d2,%d0
|;;   eor.w     d4,d1                  | [706]
	eor.l    %d4,%d0
|;;   eor.w     d2,d1                  | [707]
	eor.l    %d2,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [708]

topd:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d1                     | [711]
	move.w   %d1,%d0
	not.l    %d0
|;;   and.w     d2,d1                  | [712]
	and.l    %d2,%d0
|;;   or.w      d4,d1                  | [713]
	or.l     %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [714]

tope:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d1                     | [717]
	move.w   %d1,%d0
	not.l    %d0
|;;   and.w     d2,d1                  | [718]
	and.l    %d2,%d0
|;;   eor.w     d2,d4                  | [719]
	move.w   %d0,%d1
	move.w   %d4,%d0
	eor.l    %d2,%d0
|;;   or.w      d4,d1                  | [720]
	move.w   %d0,%d4
	and.l    #0xffff,%d0
	or.l     %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [721]

topf:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   or.w      d2,d4                  | [724]
	mvz.w    %d2,%d0
	or.l     %d0,%d4
      move.w   %d4,%d1                 | [725]
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [726]
| 1162 "vdi/vdi_tblit.S"
twop0:
      moveq.l  #0,%d1                  | [729]
      jmp      (%a3)                   | [730]

twop1:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   and.w     d4,d1                  | [733]
	move.w   %d1,%d0
	and.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [734]

twop2:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d4                     | [737]
	move.w   %d4,%d0
	not.l    %d0
|;;   and.w     d4,d1                  | [738]
	move.w   %d0,%d4
	move.w   %d1,%d0
	and.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [739]

twop3:
      jmp      (%a3)                   | [742]

twop4:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d1                     | [745]
	move.w   %d1,%d0
	not.l    %d0
|;;   and.w     d4,d1                  | [746]
	and.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [747]

twop5:
      move.w   %d4,%d1                 | [750]
      jmp      (%a3)                   | [751]

twop6:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   eor.w     d4,d1                  | [754]
	move.w   %d1,%d0
	eor.l    %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [755]

twop7:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   or.w      d4,d1                  | [758]
	mvz.w    %d4,%d0
	or.l     %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [759]

twop8:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   or.w      d4,d1                  | [762]
	mvz.w    %d4,%d0
	or.l     %d0,%d1
|;;   not.w     d1                     | [763]
	move.w   %d1,%d0
	not.l    %d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [764]

twop9:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   eor.w     d4,d1                  | [767]
	move.w   %d1,%d0
	eor.l    %d4,%d0
|;;   not.w     d1                     | [768]
	not.l    %d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [769]

twopa:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      move.w   %d4,%d1                 | [772]
|;;   not.w     d1                     | [773]
	move.w   %d1,%d0
	not.l    %d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [774]

twopb:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d4                     | [777]
	move.w   %d4,%d0
	not.l    %d0
|;;   or.w      d4,d1                  | [778]
	move.w   %d0,%d4
	and.l    #0xffff,%d0
	or.l     %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [779]

twopc:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d1                     | [782]
	move.w   %d1,%d0
	not.l    %d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [783]

twopd:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   not.w     d1                     | [786]
	move.w   %d1,%d0
	not.l    %d0
|;;   or.w      d4,d1                  | [787]
	or.l     %d4,%d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [788]

twope:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
|;;   and.w     d4,d1                  | [791]
	move.w   %d1,%d0
	and.l    %d4,%d0
|;;   not.w     d1                     | [792]
	not.l    %d0
	move.w   %d0,%d1
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
      jmp      (%a3)                   | [793]

twopf:
      moveq.l  #(-1),%d1               | [796]
      jmp      (%a3)                   | [797]
| 1250 "vdi/vdi_tblit.S"
thknop:
|***	SAVE	d2	***
	lea      -4(%a7),%a7
	movem.l  %d2,(%a7)
|;;   movem.l   d5-d7,-(sp)            | [800]
	lea      -12(%a7),%a7
	movem.l  %d5-%d7,(%a7)
|;;   and.w     d2,d1                  | [801]
	move.w   %d1,%d6
	and.l    %d2,%d6
|;;   move.w    -34(a6),d6             | [802]
	move.w   %d6,%d1
	move.w   (-34,%a6),%d6
      btst     #0,(-80+1,%a6)          | [803 EQ]
      jeq      thk00                   | [804]
|;;   ori.b     #0x10,ccr              | [805 X]
	move.w   %CCR,%d2
	ori.l    #(0x1F&0x10),%d2
	move.w   %d2,%CCR
|;;   roxr.w    #1,d6                  | [806 CC]
	move.w   %d6,%d2
	addx.l   %d6,%d6
	swap     %d6
	move.w   %d2,%d6
	lsr.l    #1,%d6
      jcc      thk01                   | [807]
      move.w   #0x8000,%d6             | [808]
thk01:
      move.w   %d6,(-34,%a6)           | [810]
thk00:
      tst.w    (-28,%a6)               | [812 MI]
      jmi      thk0                    | [813]
      cmp.w    (-36,%a6),%d6           | [814 CC]
      jcc      thk0                    | [815]
|;;   and.w     d6,d0                  | [816]
	and.l    %d6,%d0
      jra      thk1                    | [817]
thk0:
      clr.w    %d0                     | [819]
|;;   and.w     d6,d1                  | [820]
	move.w   %d1,%d7
	and.l    %d6,%d7
	move.w   %d7,%d1
thk1:
      move.w   (-74,%a6),%d6           | [822]
      move.w   %d2,%d5                 | [823]
      swap     %d5                     | [824]
      move.w   (-36,%a6),%d5           | [825]
      lsl.l    %d6,%d5                 | [826]

      swap     %d5                     | [828]
      move.w   %d5,%d7                 | [829]
      swap     %d5                     | [830]

|;;   and.w     d7,d1                  | [832]
	move.w   %d1,%d2
	and.l    %d7,%d2
|;;   and.w     d5,d0                  | [833]
	and.l    %d5,%d0
|;;   swap      d1                     | [834]
	move.w   %d2,%d1
	swap     %d1
      move.w   %d0,%d1                 | [835]
|;;   subq.w    #1,d6                  | [836]
	subq.l   #1,%d6
thkoplp:
      move.l   %d1,%d0                 | [838]
      lsr.l    #1,%d0                  | [839]
      or.l     %d0,%d1                 | [840]
|;;   dbra      d6,thkoplp             | [841]
	tst.w    %d6
	jeq      _PA_840_
	subq.l   #1,%d6
	jra      thkoplp
	_PA_840_:         

      move.w   %d1,%d0                 | [843]
      swap     %d1                     | [844]
|;;   movem.l   (sp)+,d5-d7            | [845]
	movem.l  (%a7),%d5-%d7
	lea      12(%a7),%a7
      movea.l  (-48,%a6),%a2           | [846]
|***	RESTORE	AFTER d2	***
	move.l   (%a7)+,%d2
      jmp      (%a2)                   | [847]
| 1313 "vdi/vdi_tblit.S"
thknopw:
|;;   movem.l   d5-d6,-(sp)            | [850]
	lea      -8(%a7),%a7
	movem.l  %d5-%d6,(%a7)
|;;   subq.w    #1,-30(a6)             | [851 NE]
	move.w   (-30,%a6),%d5
	subq.l   #1,%d5
	move.w   %d5,(-30,%a6)
      jne      thk2                    | [852]
|;;   and.w     -34(a6),d1             | [853]
	move.w   %d1,%d6
	move.w   (-34,%a6),%d5
	and.l    %d5,%d6
	move.w   %d6,%d1
thk2:
      move.w   (-74,%a6),%d6           | [855]
      swap     %d1                     | [856]
      clr.w    %d1                     | [857]
      move.l   %d1,%d5                 | [858]
|;;   subq.w    #1,d6                  | [859]
	subq.l   #1,%d6
thkopwlp:
      lsr.l    #1,%d5                  | [861]
      or.l     %d5,%d1                 | [862]
|;;   dbra      d6,thkopwlp            | [863]
	tst.w    %d6
	jeq      _PA_862_
	subq.l   #1,%d6
	jra      thkopwlp
	_PA_862_:         

      move.w   %d1,%d5                 | [865]
      swap     %d1                     | [866]
|;;   or.w      -82(a6),d1             | [867]
	mvz.w    (-82,%a6),%d6
	or.l     %d6,%d1
      move.w   %d5,(-82,%a6)           | [868]
|;;   movem.l   (sp)+,d5-d6            | [869]
	movem.l  (%a7),%d5-%d6
	lea      8(%a7),%a7
      movea.l  (-68,%a6),%a2           | [870]
      jmp      (%a2)                   | [871]
| 1351 "vdi/vdi_tblit.S"
thknopwf:
|;;   movem.l   d5-d7,-(sp)            | [874]
	lea      -12(%a7),%a7
	movem.l  %d5-%d7,(%a7)
      move.w   (-34,%a6),%d6           | [875]
|;;   subq.w    #1,-30(a6)             | [876 EQ MI]
	move.w   (-30,%a6),%d5
	subq.l   #1,%d5
	move.w   %d5,(-30,%a6)
      jmi      thk3                    | [877]
      jeq      thk4                    | [878]

|;;   subq.w    #1,-30(a6)             | [880 NE]
	move.w   (-30,%a6),%d5
	subq.l   #1,%d5
	move.w   %d5,(-30,%a6)
      jne      thk11                   | [881]
|;;   and.w     d6,d1                  | [882]
	move.w   %d1,%d5
	and.l    %d6,%d5
	move.w   %d5,%d1
thk11:
|;;   and.w     -38(a6),d1             | [884]
	move.w   %d1,%d6
	move.w   (-38,%a6),%d5
	and.l    %d5,%d6
	move.w   %d6,%d1
      jra      thk7                    | [885]

thk3:
      clr.w    %d1                     | [888]
      jra      thk5                    | [889]

thk4:
|;;   and.w     d6,d1                  | [892]
	move.w   %d1,%d5
	and.l    %d6,%d5
	move.w   %d5,%d1

thk5:
|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      tst.w    (-80,%a6)               | [895 MI]
      jmi      thk6                    | [896]
      move.w   (-28,%a6),%d5           | [897]
|;;   addq.w    #2,d5                  | [898]
	addq.l   #2,%d5
      move.w   (-36,%a6),%d7           | [899]
|;;   ori.b     #0x10,ccr              | [900 X]
	move.w   %CCR,%d0
	ori.l    #(0x1F&0x10),%d0
	move.w   %d0,%CCR
|;;   roxr.w    #1,d7                  | [901 CC]
	move.w   %d7,%d0
	addx.l   %d7,%d7
	swap     %d7
	move.w   %d0,%d7
	lsr.l    #1,%d7
      jcc      thk33                   | [902]
      move.w   #0x8000,%d7             | [903]
|;;   addq.w    #1,d5                  | [904]
	addq.l   #1,%d5
thk33:
|;;   cmpi.w    #1,-38(a6)             | [906 NE]
	move.w   (-38,%a6),%d0
	cmp.w    #1,%d0
      jne      thk34                   | [907]
|;;   subq.w    #1,d5                  | [908]
	subq.l   #1,%d5
thk34:
|;;   ori.b     #0x10,ccr              | [910 X]
	move.w   %CCR,%d0
	ori.l    #(0x1F&0x10),%d0
	move.w   %d0,%CCR
|;;   roxr.w    #1,d6                  | [911 CC]
	move.w   %d6,%d0
	addx.l   %d6,%d6
	swap     %d6
	move.w   %d0,%d6
	lsr.l    #1,%d6
      jcc      thk31                   | [912]
      move.w   #0x8000,%d6             | [913]
thk31:
      cmp.w    %d6,%d7                 | [915 CS]
      jcs      thk35                   | [916]
|;;   addq.w    #1,d5                  | [917]
	addq.l   #1,%d5
thk35:
      move.w   %d6,(-34,%a6)           | [919]
      move.w   %d5,(-32,%a6)           | [920]
thk6:
      move.w   (-32,%a6),(-30,%a6)     | [922]
|***	RESTORE	AFTER d0	***
	move.l   (%a7)+,%d0
thk7:
      move.w   (-74,%a6),%d6           | [924]
      swap     %d1                     | [925]
      clr.w    %d1                     | [926]
      move.l   %d1,%d5                 | [927]
|;;   subq.w    #1,d6                  | [928]
	subq.l   #1,%d6
thkopwl:
      lsr.l    #1,%d5                  | [930]
      or.l     %d5,%d1                 | [931]
|;;   dbra      d6,thkopwl             | [932]
	tst.w    %d6
	jeq      _PA_931_
	subq.l   #1,%d6
	jra      thkopwl
	_PA_931_:         

      move.w   %d1,%d5                 | [934]
      swap     %d1                     | [935]
|;;   or.w      -82(a6),d1             | [936]
	mvz.w    (-82,%a6),%d6
	or.l     %d6,%d1
      move.w   %d5,(-82,%a6)           | [937]
|;;   and.w     d2,d1                  | [938]
	move.w   %d1,%d5
	and.l    %d2,%d5
	move.w   %d5,%d1
|;;   movem.l   (sp)+,d5-d7            | [939]
	movem.l  (%a7),%d5-%d7
	lea      12(%a7),%a7
      movea.l  (-60,%a6),%a2           | [940]
      jmp      (%a2)                   | [941]
| 1432 "vdi/vdi_tblit.S"
liteop:
|***	SAVE	d2-d3	***
	lea      -8(%a7),%a7
	movem.l  %d2-%d3,(%a7)
|;;   and.w     -78(a6),d1             | [944]
	move.w   %d1,%d3
	move.w   (-78,%a6),%d2
	and.l    %d2,%d3
|;;   and.w     -78(a6),d0             | [945]
	move.w   %d3,%d1
	move.w   %d0,%d3
	move.w   (-78,%a6),%d2
	and.l    %d2,%d3
|;;   rol.w     -78(a6)                | [946]
	move.w   %d3,%d0
	clr.l    %d3
	move.w   (-78,%a6),%d2
	move.w   %d2,%d3
	lsl.l    #1,%d2
	lsr.l    #8,%d3
	lsr.l    #7,%d3
	or.l     %d3,%d2
	move.w   %d2,(-78,%a6)
      movea.l  (-52,%a6),%a2           | [947]
|***	RESTORE	AFTER d2-d3	***
	movem.l  (%a7),%d2-%d3
	lea      8(%a7),%a7
      jmp      (%a2)                   | [948]

liteopw:
|***	SAVE	d0/d2	***
	lea      -8(%a7),%a7
	movem.l  %d0/%d2,(%a7)
|;;   and.w     -78(a6),d1             | [951]
	move.w   %d1,%d2
	move.w   (-78,%a6),%d0
	and.l    %d0,%d2
      movea.l  (-72,%a6),%a2           | [952]
	move.w   %d2,%d1
|***	RESTORE	AFTER d0/d2	***
	movem.l  (%a7),%d0/%d2
	lea      8(%a7),%a7
      jmp      (%a2)                   | [953]

liteopwf:
|***	SAVE	d0/d2	***
	lea      -8(%a7),%a7
	movem.l  %d0/%d2,(%a7)
|;;   and.w     -78(a6),d1             | [956]
	move.w   %d1,%d2
	move.w   (-78,%a6),%d0
	and.l    %d0,%d2
      movea.l  (-64,%a6),%a2           | [957]
	move.w   %d2,%d1
|***	RESTORE	AFTER d0/d2	***
	movem.l  (%a7),%d0/%d2
	lea      8(%a7),%a7
      jmp      (%a2)                   | [958]
| 1464 "vdi/vdi_tblit.S"
skewop:
|***	SAVE	d3/d5	***
	lea      -8(%a7),%a7
	movem.l  %d3/%d5,(%a7)
|;;   rol.w     -80(a6)                | [961 CC]
	clr.l    %d5
	move.w   (-80,%a6),%d3
	move.w   %d3,%d5
	lsl.l    #1,%d3
	lsr.l    #8,%d5
	lsr.l    #7,%d5
	or.l     %d5,%d3
	lsr.l    #1,%d5
	move     %ccr,%d5
	move.w   %d3,(-80,%a6)
	move     %d5,%ccr
      jcc      no_shift                | [962]

      swap     %d1                     | [964]
      move.w   %d0,%d1                 | [965]
      lsr.l    #1,%d1                  | [966]

      swap     %d2                     | [968]
      move.w   (-36,%a6),%d2           | [969]
      lsr.l    #1,%d2                  | [970]
      move.w   %d2,(-36,%a6)           | [971]
      swap     %d2                     | [972]
      move.w   %d2,(-38,%a6)           | [973 EQ]
      jeq      nxt_word                | [974]
      move.w   (-42,%a6),%d0           | [975 MI]
      jmi      dec_rol                 | [976]
ror_add:
|;;   addq.w    #1,d0                  | [978]
	move.w   %d0,%d3
	addq.l   #1,%d3
	move.w   %d3,%d0
new_shif:
      move.w   %d0,(-42,%a6)           | [980]
      move.w   %d1,%d0                 | [981]
      swap     %d1                     | [982]
no_shift:
      movea.l  (-56,%a6),%a2           | [984]
|***	RESTORE	AFTER d3/d5	***
	movem.l  (%a7),%d3/%d5
	lea      8(%a7),%a7
      jmp      (%a2)                   | [985]

dec_rol:
      tst.b    %d0                     | [988 EQ]
      jeq      begn_ror                | [989]
|;;   subq.w    #1,d0                  | [990]
	move.w   %d0,%d3
	subq.l   #1,%d3
	move.w   %d3,%d0
      jra      new_shif                | [991]
begn_ror:
      clr.w    %d0                     | [993]
      jra      ror_add                 | [994]



nxt_word:
      move.w   %d2,(-36,%a6)           | [999]
      swap     %d2                     | [1000]
      move.w   %d2,(-38,%a6)           | [1001]
|;;   adda.w    -88(a6),a1             | [1002]
	mvs.w    (-88,%a6),%d0
	adda.l   %d0,%a1
      move.w   (%a1),%d4               | [1003]

      moveq.l  #15,%d0                 | [1005]
|;;   sub.w     -42(a6),d0             | [1006]
	move.w   %d0,%d5
	move.w   (-42,%a6),%d3
	sub.l    %d3,%d5
|;;   ori.w     #0x8000,d0             | [1007]
	move.w   %d5,%d0
	ori.l    #(0xFFFF&0x8000),%d0
      move.w   %d0,(-42,%a6)           | [1008]
      jra      no_shift                | [1009]






skewopw:
|;;   rol.w     -80(a6)                | [1017 CC]
	clr.l    %d7
	move.w   (-80,%a6),%d6
	move.w   %d6,%d7
	lsl.l    #1,%d6
	lsr.l    #8,%d7
	lsr.l    #7,%d7
	or.l     %d7,%d6
	lsr.l    #1,%d7
	move     %ccr,%d7
	move.w   %d6,(-80,%a6)
	move     %d7,%ccr
      jcc      do_shift                | [1018]

      tst.w    (-42,%a6)               | [1020 MI]
      jmi      mlt_left                | [1021]
      jra      mlt_rite                | [1022]
do_shift:
|;;   ori       #0x10,ccr              | [1024 X]
	move.w   %CCR,%d6
	ori.l    #(0x1F&0x10),%d6
	move.w   %d6,%CCR
|;;   roxr.w    -40(a6)                | [1025]
	clr.l    %d7
	move.w   (-40,%a6),%d6
	subx.l   %d7,%d7
	move.w   %d6,%d7
	move.l   %d7,%d6
	asr.l    #1,%d6
	move.w   %d6,(-40,%a6)

      move.w   (-36,%a6),%d0           | [1027]
      cmp.w    #0xFFFF,%d0             | [1028 EQ]
      jeq      inc_rite                | [1029]

|;;   ori       #0x10,ccr              | [1031 X]
	move.w   %CCR,%d6
	ori.l    #(0x1F&0x10),%d6
	move.w   %d6,%CCR
|;;   roxr.w    #1,d0                  | [1032]
	move.w   %d0,%d6
	move.w   %d6,%d7
	addx.l   %d6,%d6
	swap     %d6
	move.w   %d7,%d6
	lsr.l    #1,%d6
|;;   move.w    d0,-36(a6)             | [1033]
	move.w   %d6,(-36,%a6)

do_left:
      move.w   (-42,%a6),%d0           | [1036]
      tst.b    %d0                     | [1037 NE]
      jne      no_rota                 | [1038]
      move.w   #0x8000,(-40,%a6)       | [1039]
no_rota:
      move.w   (-38,%a6),%d1           | [1041]
|;;   lsr.w     #1,d1                  | [1042 EQ]
	mvz.w    %d1,%d6
	lsr.l    #1,%d6
	move     %ccr,%d7
	move.w   %d6,%d1
	move     %d7,%ccr
      jeq      wnxt_wrd                | [1043]

      move.w   %d1,(-38,%a6)           | [1045]

      tst.w    %d0                     | [1047 MI]
      jmi      wdec_rol                | [1048]
|;;   addq.w    #1,-42(a6)             | [1049]
	move.w   (-42,%a6),%d6
	addq.l   #1,%d6
	move.w   %d6,(-42,%a6)
      jra      mlt_rite                | [1050]
wdec_rol:
      tst.b    %d0                     | [1052 EQ]
      jeq      set_msk                 | [1053]
|;;   subq.w    #1,-42(a6)             | [1054]
	move.w   (-42,%a6),%d0
	subq.l   #1,%d0
	move.w   %d0,(-42,%a6)
      jra      mlt_left                | [1055]
set_msk:
      move.w   #1,(-42,%a6)            | [1057]
      jra      mlt_rite                | [1058]

inc_rite:
|;;   addq.w    #1,-28(a6)             | [1061]
	move.w   (-28,%a6),%d6
	addq.l   #1,%d6
	move.w   %d6,(-28,%a6)
      move.w   #0x8000,(-36,%a6)       | [1062]
      jra      do_left                 | [1063]

wnxt_wrd:
      move.w   #0xFFFF,(-38,%a6)       | [1066]

|;;   subq.w    #1,-28(a6)             | [1068]
	move.w   (-28,%a6),%d2
	subq.l   #1,%d2
	move.w   %d2,(-28,%a6)
|;;   adda.w    -88(a6),a1             | [1069]
	mvs.w    (-88,%a6),%d2
	adda.l   %d2,%a1

      moveq.l  #15,%d2                 | [1071]
|;;   sub.w     d0,d2                  | [1072]
	move.w   %d2,%d6
	sub.l    %d0,%d6
|;;   ori.w     #0x8000,d2             | [1073]
	move.w   %d6,%d2
	ori.l    #(0xFFFF&0x8000),%d2
      move.w   %d2,(-42,%a6)           | [1074]

      jra      mlt_left                | [1076]




rotation:
|***	SAVE	d5	***
	lea      -4(%a7),%a7
	movem.l  %d5,(%a7)
      move.w   _linea_vars+982,%d1     | [1082]
      move.w   %d1,%d2                 | [1083]
|;;   andi.w    #0x0F,d2               | [1084]
	andi.l   #(0xFFFF&0x0F),%d2
      move.w   %d2,(-24,%a6)           | [1085]
|;;   lsr.w     #4,d1                  | [1086]
	and.l    #0xffff,%d1
	lsr.l    #4,%d1
|;;   add.w     d1,d1                  | [1087]
	add.l    %d1,%d1

      movea.l  (-10,%a6),%a0           | [1089]
|;;   adda.w    d1,a0                  | [1090]
	mvs.w    %d1,%d0
	adda.l   %d0,%a0

      move.w   (-102,%a6),(-20,%a6)    | [1092]
      move.w   _linea_vars+984,%d0     | [1093]
      move.w   (-106,%a6),%d1          | [1094]
      move.w   %d1,(-22,%a6)           | [1095]
      move.w   (-16,%a6),%d2           | [1096]

      tst.w    (-126,%a6)              | [1098 EQ LT]
      jeq      upsd_dwn                | [1099]
rot90:
      jlt      top_src                 | [1101]
|;;   neg.w     -16(a6)                | [1102]
	move.w   (-16,%a6),%d5
	neg.l    %d5
	move.w   %d5,(-16,%a6)
|;;   subq.w    #1,d1                  | [1103]
	subq.l   #1,%d1
|;;   add.w     d1,d0                  | [1104]
	add.l    %d1,%d0
      mulu.w   %d2,%d0                 | [1105]
      adda.l   %d0,%a0                 | [1106]
top_src:
      move.w   (-106,%a6),%d0          | [1108]
|;;   lsr.w     #4,d0                  | [1109]
	and.l    #0xffff,%d0
	lsr.l    #4,%d0
|;;   add.w     d0,d0                  | [1110]
	add.l    %d0,%d0
|;;   addq.w    #2,d0                  | [1111]
	addq.l   #2,%d0
      move.w   %d0,(-18,%a6)           | [1112]
      movea.l  _linea_vars+1018,%a1    | [1113]
|;;   adda.w    -4(a6),a1              | [1114]
	mvs.w    (-4,%a6),%d5
	adda.l   %d5,%a1

      tst.w    (-126,%a6)              | [1116 GT]
      jgt      top_dwn                 | [1117]
|;;   neg.w     -18(a6)                | [1118]
	move.w   (-18,%a6),%d5
	neg.l    %d5
	move.w   %d5,(-18,%a6)
      move.w   (-102,%a6),%d1          | [1119]
|;;   subq.w    #1,d1                  | [1120]
	subq.l   #1,%d1
      mulu.w   %d1,%d0                 | [1121]
      adda.l   %d0,%a1                 | [1122]
top_dwn:
      move.w   (-24,%a6),%d2           | [1124]
      move.w   #0x8000,%d3             | [1125]
      move.w   %d3,%d4                 | [1126]
      movea.w  %d3,%a4                 | [1127]
|;;   lsr.w     d2,d3                  | [1128]
	and.l    #0xffff,%d3
	lsr.l    %d2,%d3
      moveq.l  #0,%d0                  | [1129]
      move.w   (-20,%a6),%d2           | [1130]
      move.w   (-16,%a6),%d6           | [1131]
      movea.l  %a0,%a2                 | [1132]
      movea.l  %a1,%a3                 | [1133]
      jra      rot_nsrc                | [1134]
rot_ylp:
      move.w   (-22,%a6),%d1           | [1136]
      jra      rot_srt                 | [1137]
rot_xlp:
      move.w   (%a0),%d7               | [1139]
|;;   and.w     d3,d7                  | [1140 EQ]
	and.l    %d3,%d7
	tst.w    %d7
      jeq      rot_nor                 | [1141]
|;;   or.w      d4,d0                  | [1142]
	or.l     %d4,%d0
rot_nor:
|;;   ror.w     #1,d4                  | [1144 CC]
	move.w   %d4,%d5
	swap     %d4
	move.w   %d5,%d4
	lsr.l    #1,%d4
      jcc      rot_isrc                | [1145]
rot_ndst:
      move.w   %d0,(%a1)+              | [1147]
      moveq.l  #0,%d0                  | [1148]
rot_isrc:
|;;   adda.w    d6,a0                  | [1150]
	mvs.w    %d6,%d5
	adda.l   %d5,%a0
rot_srt:
|;;   dbra      d1,rot_xlp             | [1152]
	tst.w    %d1
	jeq      _PA_1151_
	subq.l   #1,%d1
	jra      rot_xlp
	_PA_1151_:         

      move.w   %d0,(%a1)               | [1154]
      moveq.l  #0,%d0                  | [1155]
|;;   adda.w    -18(a6),a3             | [1156]
	mvs.w    (-18,%a6),%d5
	adda.l   %d5,%a3
      movea.l  %a3,%a1                 | [1157]
      move.w   %a4,%d4                 | [1158]
|;;   ror.w     #1,d3                  | [1159 CC]
	move.w   %d3,%d5
	swap     %d3
	move.w   %d5,%d3
	lsr.l    #1,%d3
      jcc      rnew_src                | [1160]
      addq.l   #2,%a2                  | [1161]
rnew_src:
      movea.l  %a2,%a0                 | [1163]
rot_nsrc:
|;;   dbra      d2,rot_ylp             | [1165]
	tst.w    %d2
	jeq      _PA_1164_
	subq.l   #1,%d2
	jra      rot_ylp
	_PA_1164_:         

rot_done:
      move.w   (-102,%a6),%d0          | [1168]
      move.w   (-106,%a6),%d1          | [1169]
      move.w   %d1,(-20,%a6)           | [1170]
      move.w   %d1,(-102,%a6)          | [1171]
      move.w   %d0,(-22,%a6)           | [1172]
      move.w   %d0,(-106,%a6)          | [1173]

      move.w   (-92,%a6),%d0           | [1175]
      move.w   (-90,%a6),(-92,%a6)     | [1176]
      move.w   %d0,(-90,%a6)           | [1177]
      move.w   #1,(-94,%a6)            | [1178]

      move.w   (-18,%a6),%d0           | [1180]
      tst.w    (-126,%a6)              | [1181 GT]
      jgt      rot_nneg                | [1182]
|;;   neg.w     d0                     | [1183]
	neg.l    %d0
rot_nneg:
      move.w   %d0,(-16,%a6)           | [1185]
      move.w   (-4,%a6),%d0            | [1186]
|***	RESTORE	AFTER d5	***
	move.l   (%a7)+,%d5
repexit:
      clr.w    _linea_vars+982         | [1188]
repexit3:
      movea.l  _linea_vars+1018,%a2    | [1190]
|;;   adda.w    d0,a2                  | [1191]
	ext.l    %d0
	adda.l   %d0,%a2
      move.l   %a2,(-10,%a6)           | [1192]
      clr.w    _linea_vars+984         | [1193]
      rts                              | [1194]

upsd_dwn:
      move.w   (-102,%a6),%d0          | [1197]
|;;   add.w     -24(a6),d0             | [1198]
	move.w   (-24,%a6),%d5
	add.l    %d5,%d0
|;;   subq.w    #1,d0                  | [1199]
	subq.l   #1,%d0
|;;   lsr.w     #4,d0                  | [1200]
	and.l    #0xffff,%d0
	lsr.l    #4,%d0
|;;   add.w     d0,d0                  | [1201]
	add.l    %d0,%d0
|;;   addq.w    #2,d0                  | [1202]
	addq.l   #2,%d0

      move.w   %d0,(-18,%a6)           | [1204]
      move.w   %d0,%d2                 | [1205]
|;;   lsr.w     #1,d2                  | [1206]
	lsr.l    #1,%d2
|;;   subq.w    #1,d2                  | [1207]
	subq.l   #1,%d2
      mulu.w   %d1,%d0                 | [1208]
|;;   add.w     -4(a6),d0              | [1209]
	move.w   (-4,%a6),%d5
	add.l    %d5,%d0
      movea.l  _linea_vars+1018,%a1    | [1210]
|;;   adda.w    d0,a1                  | [1211]
	ext.l    %d0
	adda.l   %d0,%a1
      jra      strtflip                | [1212]
upsd_lp:
      movea.l  %a0,%a2                 | [1214]
      move.w   %d2,%d3                 | [1215]
line_lp:
      move.w   (%a2)+,%d0              | [1217]
      moveq.l  #0,%d6                  | [1218]
      moveq.l  #15,%d4                 | [1219]
flip_lp:
|;;   lsr.w     #1,d0                  | [1221 X]
	and.l    #0xffff,%d0
	lsr.l    #1,%d0
|;;   addx.w    d6,d6                  | [1222]
	addx.l   %d6,%d6
|;;   dbra      d4,flip_lp             | [1223]
	subq.l   #1,%d4
	jpl      flip_lp

      move.w   %d6,-(%a1)              | [1225]
|;;   dbra      d3,line_lp             | [1226]
	tst.w    %d3
	jeq      _PA_1225_
	subq.l   #1,%d3
	jra      line_lp
	_PA_1225_:         

|;;   adda.w    -16(a6),a0             | [1228]
	mvs.w    (-16,%a6),%d5
	adda.l   %d5,%a0
strtflip:
|;;   dbra      d1,upsd_lp             | [1230]
	tst.w    %d1
	jeq      _PA_1229_
	subq.l   #1,%d1
	jra      upsd_lp
	_PA_1229_:         

      move.w   (-18,%a6),(-16,%a6)     | [1232]
      movea.l  _linea_vars+1018,%a1    | [1233]
|;;   adda.w    -4(a6),a1              | [1234]
	mvs.w    (-4,%a6),%d5
	adda.l   %d5,%a1
      move.l   %a1,(-10,%a6)           | [1235]
      move.w   _linea_vars+982,%d0     | [1236]
|;;   add.w     -102(a6),d0            | [1237]
	move.w   (-102,%a6),%d5
	add.l    %d5,%d0
|;;   neg.w     d0                     | [1238]
	neg.l    %d0
|;;   andi.w    #0xF,d0                | [1239]
	andi.l   #(0xFFFF&0xF),%d0
      move.w   %d0,_linea_vars+982     | [1240]
      clr.w    _linea_vars+984         | [1241]
|***	RESTORE	d5	***
	move.l   (%a7)+,%d5
      rts                              | [1242]






replicat:
|***	SAVE	d7	***
	lea      -12(%a7),%a7
	movem.l  %d7,8(%a7)
      move.w   _linea_vars+982,%d0     | [1250]
      move.w   %d0,%d4                 | [1251]
|;;   andi.w    #0x0F,d0               | [1252]
	andi.l   #(0xFFFF&0x0F),%d0
      move.w   %d0,(-24,%a6)           | [1253]
|;;   lsr.w     #4,d4                  | [1254]
	and.l    #0xffff,%d4
	lsr.l    #4,%d4
|;;   add.w     d4,d4                  | [1255]
	add.l    %d4,%d4

      move.w   _linea_vars+984,%d0     | [1257]
      mulu.w   (-16,%a6),%d0           | [1258]
|***	SAVE	d0	***
	movem.l  %d0,(%a7)
      movea.l  (-10,%a6),%a0           | [1259]
|;;   adda.w    d4,a0                  | [1260]
	mvs.w    %d4,%d3
	adda.l   %d3,%a0
      adda.l   %d0,%a0                 | [1261]

      move.w   (-24,%a6),%d2           | [1263]
      move.w   #0x8000,%d3             | [1264]
      move.w   %d3,%d4                 | [1265]
|***	SAVE	d4	***
	movem.l  %d4,4(%a7)
|;;   lsr.w     d2,d3                  | [1266]
	and.l    #0xffff,%d3
	lsr.l    %d2,%d3

      move.w   (-106,%a6),%d2          | [1268]
      move.w   (-102,%a6),%d1          | [1269]

      move.w   %d2,(-22,%a6)           | [1271]
      move.w   %d1,(-20,%a6)           | [1272]
      movea.l  _linea_vars+1018,%a1    | [1273]
|;;   adda.w    _linea_vars+1022,a1    | [1274]
	mvs.w    _linea_vars+1022,%d0
	adda.l   %d0,%a1

      btst     #4,(-96+1,%a6)          | [1276 EQ]
      jeq      noline                  | [1277]



|;;   addq.w    #1,-106(a6)            | [1281]
	move.w   (-106,%a6),%d0
	addq.l   #1,%d0
	move.w   %d0,(-106,%a6)
|;;   addq.w    #1,-102(a6)            | [1282]
	move.w   (-102,%a6),%d0
	addq.l   #1,%d0
	move.w   %d0,(-102,%a6)

|;;   add.w     d1,d1                  | [1284]
	add.l    %d1,%d1
|;;   addq.w    #3,d1                  | [1285]
	addq.l   #3,%d1
|;;   lsr.w     #4,d1                  | [1286]
	and.l    #0xffff,%d1
	lsr.l    #4,%d1
|;;   add.w     d1,d1                  | [1287]
	add.l    %d1,%d1
|;;   addq.w    #2,d1                  | [1288]
	addq.l   #2,%d1
      move.w   %d1,%d5                 | [1289]
      moveq.l  #0,%d6                  | [1290]
oklear:
      move.w   %d6,(%a1)+              | [1292]
|;;   dbra      d5,oklear              | [1293]
	tst.w    %d5
	jeq      _PA_1292_
	subq.l   #1,%d5
	jra      oklear
	_PA_1292_:         
      lea      (-2,%a1),%a2            | [1294]
      move.w   %d2,%d5                 | [1295]
|;;   add.w     d5,d5                  | [1296]
	add.l    %d5,%d5
oklear1:
|;;   adda.w    d1,a2                  | [1298]
	mvs.w    %d1,%d0
	adda.l   %d0,%a2
      move.w   %d6,(%a2)               | [1299]
|;;   dbra      d5,oklear1             | [1300]
	tst.w    %d5
	jeq      _PA_1299_
	subq.l   #1,%d5
	jra      oklear1
	_PA_1299_:         

|;;   move.w    d1,d5                  | [1302]
	move.w   %d0,%d5
|;;   lsr.w     #1,d5                  | [1303]
	and.l    #0xffff,%d5
	lsr.l    #1,%d5
|;;   subq.w    #1,d5                  | [1304 X]
	subq.l   #1,%d5
oklear3:
      move.w   %d6,-(%a2)              | [1306]
|;;   dbra      d5,oklear3             | [1307]
	move     %CCR,%d0
	tst.w    %d5
	jeq      _PA_1306_
	subq.l   #1,%d5
	move     %d0,%CCR
	jra      oklear3
	_PA_1306_:         
	move     %d0,%CCR
      jra      noline1                 | [1308]

noline:
|;;   lsr.w     #3,d1                  | [1311]
	and.l    #0xffff,%d1
	lsr.l    #3,%d1
|;;   add.w     d1,d1                  | [1312]
	add.l    %d1,%d1
|;;   addq.w    #2,d1                  | [1313 X]
	ori.l    #0xffff0000,%d1
	addq.l   #2,%d1
noline1:
      move.w   %d1,(-18,%a6)           | [1315]
|***	RESTORE	AFTER d7	***
	movem.l  8(%a7),%d7



      move.w   _linea_vars+978,%d7     | [1319]
|;;   roxr.l    #1,d7                  | [1320 X]
	move.l   %d7,%d0
	addx.l   %d0,%d0
	moveq    #32-1,%d4
	lsl.l    %d4,%d0
	lsr.l    #1,%d7
	or.l     %d0,%d7
      moveq.l  #0,%d7                  | [1321]
|;;   roxr.l    #1,d7                  | [1322]
	move.l   %d7,%d0
	addx.l   %d0,%d0
	moveq    #32-1,%d4
	lsl.l    %d4,%d0
	lsr.l    #1,%d7
	or.l     %d0,%d7

      move.w   (-16,%a6),%d7           | [1324]
      move.w   _linea_vars+976,%d2     | [1325]
      move.w   (-22,%a6),%d5           | [1326]
|;;   subq.w    #1,d5                  | [1327]
	subq.l   #1,%d5
      move.w   #32767,%d6              | [1328]
|***	RESTORE	d0/d4	***
	movem.l  (%a7),%d0/%d4
	lea      12(%a7),%a7
      tst.l    %d7                     | [1329 MI]
      jmi      rep_ylop                | [1330]
y_dwn_lp:
|***	SAVE	d1	***
	lea      -4(%a7),%a7
	movem.l  %d1,(%a7)
|;;   add.w     d2,d6                  | [1332 CC]
	and.l    #0xffff,%d2
	ori.l    #0xffff0000,%d6
	add.l    %d2,%d6
      jcc      y_no_drw                | [1333]
      jbsr     yloop                   | [1334]
y_no_drw:
|;;   adda.w    d7,a0                  | [1336]
	mvs.w    %d7,%d1
	adda.l   %d1,%a0
|***	RESTORE	AFTER d1	***
	move.l   (%a7)+,%d1
|;;   dbra      d5,y_dwn_lp            | [1337]
	tst.w    %d5
	jeq      _PA_1336_
	subq.l   #1,%d5
	jra      y_dwn_lp
	_PA_1336_:         
      jra      y_rep_don               | [1338]
rep_ylop:
|***	SAVE	d1	***
	lea      -4(%a7),%a7
	movem.l  %d1,(%a7)
|;;   add.w     d2,d6                  | [1340 CC]
	mvz.w    %d2,%d0
	ori.l    #0xffff0000,%d6
	add.l    %d0,%d6
      jcc      y_no_rep                | [1341]
      jbsr     yloop                   | [1342]
y_no_rep:
      jbsr     yloop                   | [1344]
|;;   adda.w    d7,a0                  | [1345]
	mvs.w    %d7,%d1
	adda.l   %d1,%a0
|***	RESTORE	AFTER d1	***
	move.l   (%a7)+,%d1
|;;   dbra      d5,rep_ylop            | [1346]
	tst.w    %d5
	jeq      _PA_1345_
	subq.l   #1,%d5
	jra      rep_ylop
	_PA_1345_:         
y_rep_don:
      move.w   (-102,%a6),%d2          | [1348]
|;;   subq.w    #1,d2                  | [1349]
	subq.l   #1,%d2
      move.w   _linea_vars+974,%d1     | [1350]
      move.w   _linea_vars+976,%d0     | [1351]
      moveq.l  #0,%d3                  | [1352]
repwidcl:
|;;   add.w     d0,d1                  | [1354 CC]
	and.l    #0xffff,%d0
	ori.l    #0xffff0000,%d1
	add.l    %d0,%d1
      jcc      nrepdoub                | [1355]
|;;   addq.w    #1,d3                  | [1356]
	addq.l   #1,%d3
nrepdoub:
      tst.l    %d7                     | [1358 PL]
      jpl      nrpndoub                | [1359]
|;;   addq.w    #1,d3                  | [1360]
	addq.l   #1,%d3
nrpndoub:
|;;   dbra      d2,repwidcl            | [1362]
	tst.w    %d2
	jeq      _PA_1361_
	subq.l   #1,%d2
	jra      repwidcl
	_PA_1361_:         
      move.w   %d1,_linea_vars+974     | [1363]
      move.w   %d3,(-102,%a6)          | [1364]
      move.w   (-92,%a6),(-106,%a6)    | [1365]


      move.w   (-18,%a6),%d1           | [1368]
      move.w   %d1,(-16,%a6)           | [1369]







repexit1:
      btst     #4,(-96+1,%a6)          | [1378 EQ]
      jeq      repexit2                | [1379]
      movea.l  _linea_vars+1018,%a0    | [1380]
|;;   adda.w    _linea_vars+1022,a0    | [1381]
	mvs.w    _linea_vars+1022,%d0
	adda.l   %d0,%a0
      move.w   %d1,%d7                 | [1382]
|;;   lea       2(a0,d7.w),a1          | [1383]
	ext.l    %d7
	lea      (2,%a0,%d7.l*1),%a1
      jbsr     outlin1                 | [1384]
|***	SAVE	d1	***
	lea      -4(%a7),%a7
	movem.l  %d1,(%a7)
      move.w   _linea_vars+1022,%d0    | [1385]
|;;   add.w     -18(a6),d0             | [1386]
	move.w   (-18,%a6),%d1
	add.l    %d1,%d0
|;;   move.w    #15,_linea_vars+982    | [1387]
	moveq.l  #15,%d1
	move.w   %d1,_linea_vars+982
|***	RESTORE	d1	***
	move.l   (%a7)+,%d1
      jra      repexit3                | [1388]
repexit2:
      move.w   _linea_vars+1022,%d0    | [1390]
      jra      repexit                 | [1391]
| 1926 "vdi/vdi_tblit.S"
yloop:
|***	SAVE	d1	***
	lea      -4(%a7),%a7
	movem.l  %d1,(%a7)
|;;   movem.l   d3-d4/d6,-(sp)         | [1394]
	lea      -12(%a7),%a7
	movem.l  %d3-%d4/%d6,(%a7)
      movea.l  %a0,%a3                 | [1395]
      movea.l  %a1,%a4                 | [1396]
      movea.l  %d7,%a5                 | [1397]
      move.w   %d5,(-44,%a6)           | [1398]
      moveq.l  #0,%d0                  | [1399]
      move.w   (-20,%a6),%d5           | [1400]
|;;   subq.w    #1,d5                  | [1401]
	subq.l   #1,%d5

      move.w   _linea_vars+974,%d7     | [1403]
      jra      nextsrc                 | [1404]
innerlp:
|;;   ror.w     #1,d3                  | [1406 CC]
	move.w   %d3,%d1
	swap     %d3
	move.w   %d1,%d3
	lsr.l    #1,%d3
      jcc      reploop                 | [1407]
nextsrc:
      movea.w  (%a0)+,%a2              | [1409]
reploop:
      move.w   %a2,%d6                 | [1411]
|;;   and.w     d3,d6                  | [1412 NE]
	and.l    %d3,%d6
	tst.w    %d6
      jne      nrepnor                 | [1413]


repnor:
      tst.l    %d7                     | [1417 MI]
      jmi      repnorup                | [1418]
|;;   add.w     d2,d7                  | [1419 CC]
	and.l    #0xffff,%d2
	move.w   %d7,%d1
	ori.l    #0xffff0000,%d1
	add.l    %d2,%d1
	move     %ccr,%d6
	move.w   %d1,%d7
	move     %d6,%ccr
      jcc      incsrc                  | [1420]
      jra      ordone                  | [1421]
repnorup:
|;;   add.w     d2,d7                  | [1423 CC]
	and.l    #0xffff,%d2
	move.w   %d7,%d1
	ori.l    #0xffff0000,%d1
	add.l    %d2,%d1
	move     %ccr,%d6
	move.w   %d1,%d7
	move     %d6,%ccr
      jcc      ordone                  | [1424]


|;;   ror.w     #1,d4                  | [1427 CC]
	move.w   %d4,%d1
	swap     %d4
	move.w   %d1,%d4
	lsr.l    #1,%d4


      jcc      ordone                  | [1430]
      move.w   %d0,(%a1)+              | [1431]
      moveq.l  #0,%d0                  | [1432]


      jra      ordone                  | [1435]
nrepnor:


|;;   add.w     d2,d7                  | [1439 CC]
	and.l    #0xffff,%d2
	move.w   %d7,%d1
	ori.l    #0xffff0000,%d1
	add.l    %d2,%d1
	move     %ccr,%d6
	move.w   %d1,%d7
	move     %d6,%ccr
      jcc      o_no_rep                | [1440]


|;;   or.w      d4,d0                  | [1443]
	or.l     %d4,%d0
|;;   ror.w     #1,d4                  | [1444 CC]
	move.w   %d4,%d1
	swap     %d4
	move.w   %d1,%d4
	lsr.l    #1,%d4

      jcc      o_no_rep                | [1446]
      move.w   %d0,(%a1)+              | [1447]
      moveq.l  #0,%d0                  | [1448]
o_no_rep:
      tst.l    %d7                     | [1450 PL]
      jpl      incsrc                  | [1451]


|;;   or.w      d4,d0                  | [1454]
	or.l     %d4,%d0
ordone:
|;;   ror.w     #1,d4                  | [1456 CC]
	move.w   %d4,%d1
	swap     %d4
	move.w   %d1,%d4
	lsr.l    #1,%d4
      jcc      incsrc                  | [1457]
nextdst:
      move.w   %d0,(%a1)+              | [1459]
      moveq.l  #0,%d0                  | [1460]
incsrc:
|;;   dbra      d5,innerlp             | [1462]
	tst.w    %d5
	jeq      _PA_1461_
	subq.l   #1,%d5
	jra      innerlp
	_PA_1461_:         
repdone:
      move.w   %d0,(%a1)               | [1464]
|;;   movem.l   (sp)+,d3-d4/d6         | [1465]
	movem.l  (%a7),%d3-%d4/%d6
	lea      12(%a7),%a7
      movea.l  %a3,%a0                 | [1466]
      movea.l  %a4,%a1                 | [1467]
|***	RESTORE	AFTER d1	***
	move.l   (%a7)+,%d1
|;;   adda.w    d1,a1                  | [1468]
	ext.l    %d1
	adda.l   %d1,%a1
      move.l   %a5,%d7                 | [1469]
      move.w   (-44,%a6),%d5           | [1470]
      rts                              | [1471]
| 2014 "vdi/vdi_tblit.S"
outlin:
|;;   lea       (a0,d7.w),a1           | [1474]
	ext.l    %d7
	lea      (0,%a0,%d7.l*1),%a1
outlin1:
|;;   lea       (a1,d7.w),a2           | [1476]
	ext.l    %d7
	lea      (0,%a1,%d7.l*1),%a2
|;;   lsr.w     #1,d7                  | [1477]
	and.l    #0xffff,%d7
	lsr.l    #1,%d7
|;;   subq.w    #1,d7                  | [1478]
	subq.l   #1,%d7
      move.w   %d7,(-44,%a6)           | [1479]
      move.w   (-106,%a6),%d6          | [1480]

      movea.l  %a0,%a5                 | [1482]
|;;   subq.w    #1,d6                  | [1483]
	subq.l   #1,%d6
out_edge:
      movea.w  %d6,%a4                 | [1485]
      movea.l  %a2,%a3                 | [1486]
      moveq.l  #0,%d5                  | [1487]
      moveq.l  #0,%d6                  | [1488]
      move.l   (%a2),%d1               | [1489]
      lsr.l    #1,%d1                  | [1490]



out_loop:
|***	SAVE	d1/d7	***
	lea      -16(%a7),%a7
	movem.l  %d1,4(%a7)
	movem.l  %d7,12(%a7)
      move.l   (%a0),%d0               | [1495]
      move.b   %d5,%d0                 | [1496]
|;;   ror.l     #1,d0                  | [1497]
	move.l   %d0,%d1
	lsr.l    #1,%d0
	moveq    #32-1,%d2
	lsl.l    %d2,%d1
	or.l     %d1,%d0

      move.l   (%a1),%d2               | [1499]
      move.b   %d6,%d2                 | [1500]
      move.l   %d2,%d3                 | [1501]
|;;   ror.l     #1,d3                  | [1502]
	move.l   %d3,%d1
	lsr.l    #1,%d3
	moveq    #32-1,%d4
	lsl.l    %d4,%d1
	or.l     %d1,%d3
      move.l   %d3,%d4                 | [1503]
|;;   ror.l     #1,d4                  | [1504]
	move.l   %d4,%d1
	lsr.l    #1,%d4
	moveq    #32-1,%d5
	lsl.l    %d5,%d1
	or.l     %d1,%d4

      move.l   %d0,%d5                 | [1506]
      move.l   %d0,%d6                 | [1507]
      eor.l    %d2,%d0                 | [1508]
      eor.l    %d3,%d5                 | [1509]
      eor.l    %d4,%d6                 | [1510]
|;;   rol.l     #1,d5                  | [1511]
	clr.l    %d1
	lsl.l    #1,%d5
	addx.l   %d1,%d5
|;;   rol.l     #2,d6                  | [1512]
	move.l   %d6,%d1
	lsl.l    #2,%d6
	moveq    #32-2,%d7
	lsr.l    %d7,%d1
	or.l     %d1,%d6
      or.l     %d5,%d0                 | [1513]
      or.l     %d6,%d0                 | [1514]
|***	RESTORE	AFTER d1	***
	movem.l  4(%a7),%d1

|***	SAVE	d0	***
	movem.l  %d0,(%a7)
      move.l   %d1,%d5                 | [1516]
      move.l   %d1,%d6                 | [1517]
      eor.l    %d2,%d1                 | [1518]
      eor.l    %d3,%d5                 | [1519]
      eor.l    %d4,%d6                 | [1520]
|;;   rol.l     #1,d5                  | [1521]
	clr.l    %d0
	lsl.l    #1,%d5
	addx.l   %d0,%d5
|;;   rol.l     #2,d6                  | [1522]
	move.l   %d6,%d0
	lsl.l    #2,%d6
	moveq    #32-2,%d7
	lsr.l    %d7,%d0
	or.l     %d0,%d6
|***	RESTORE	AFTER d0	***
	movem.l  (%a7),%d0
      or.l     %d1,%d0                 | [1523]
      or.l     %d5,%d0                 | [1524]
      or.l     %d6,%d0                 | [1525]

      eor.l    %d3,%d2                 | [1527]
      eor.l    %d3,%d4                 | [1528]
|***	SAVE	d3	***
	movem.l  %d3,8(%a7)
|;;   rol.l     #2,d4                  | [1529]
	move.l   %d4,%d1
	lsl.l    #2,%d4
	moveq    #32-2,%d3
	lsr.l    %d3,%d1
	or.l     %d1,%d4
      or.l     %d2,%d0                 | [1530]
      or.l     %d4,%d0                 | [1531]
      swap     %d0                     | [1532]

      move.w   (%a1),%d6               | [1534]
      move.w   %d6,%d5                 | [1535]
|;;   eor.w     d0,d5                  | [1536]
	eor.l    %d0,%d5
|;;   and.w     d0,d5                  | [1537]
	and.l    %d0,%d5

      addq.l   #2,%a2                  | [1539]
      move.l   (%a2),%d1               | [1540]
      move.b   (-1,%a2),%d1            | [1541]
|;;   ror.l     #1,d1                  | [1542]
	move.l   %d1,%d3
	lsr.l    #1,%d1
	moveq    #32-1,%d7
	lsl.l    %d7,%d3
	or.l     %d3,%d1

      move.w   %d5,(%a1)+              | [1544]
      move.w   (%a0),%d5               | [1545]
      move.w   %d6,(%a0)+              | [1546]
|***	RESTORE	AFTER d3/d7	***
	movem.l  8(%a7),%d3/%d7
	lea      16(%a7),%a7

|;;   dbra      d7,out_loop            | [1548]
	tst.w    %d7
	jeq      _PA_1547_
	subq.l   #1,%d7
	jra      out_loop
	_PA_1547_:         

|***	SAVE	d0	***
	lea      -4(%a7),%a7
	movem.l  %d0,(%a7)
      move.w   (-44,%a6),%d7           | [1550]
      movea.l  %a5,%a0                 | [1551]
      move.w   %a4,%d6                 | [1552]
      movea.l  %a3,%a2                 | [1553]
      movea.l  %a2,%a1                 | [1554]
|;;   adda.w    -16(a6),a2             | [1555]
	mvs.w    (-16,%a6),%d0
	adda.l   %d0,%a2
|***	RESTORE	d0	***
	move.l   (%a7)+,%d0
      cmp.w    #1,%d6                  | [1556 NE]
      jne      srt_lin                 | [1557]
      movea.l  %a1,%a2                 | [1558]
srt_lin:
|;;   dbra      d6,out_edge            | [1560]
	tst.w    %d6
	jeq      _PA_1559_
	subq.l   #1,%d6
	jra      out_edge
	_PA_1559_:         
      rts                              | [1561]

| End section on aligned boundary
	.p2align 2,0
