#
# bios/build.mk - objects making up the BIOS
#
# This file is included by the top level Makefile.  Objects are named
# without any directory: the actual source file is looked up through vpath
# in bios/machine/$(MACHINE), bios/arch/$(ARCH) and finally bios, so that
# an architecture can override a generic implementation just by providing
# a file of the same name.
#

# The startup code must be the very first object linked into the image.
obj-y += startup.o

# This one is placed in ST-RAM by the linker script.
obj-y += lowstram.o

# The remaining BIOS objects can come in any order.
obj-y += memory.o processor.o intmask.o vectors.o bios.o xbios.o acsi.o biosmem.o \
	 blkdev.o chardev.o clock.o conout.o cookie.o country.o disk.o \
	 dma.o dmasound.o floppy.o font.o ide.o ikbd.o initinfo.o kprint.o \
	 lineainit.o machine.o mfp.o midi.o mouse.o nvram.o panicasm.o \
	 parport.o screen.o serport.o sound.o videl.o vt52.o xhdi.o delay.o \
	 sd.o memory2.o bootparams.o scsi.o

obj-$(ARCH_M68K) += aciavecs.o kprintasm.o linea.o natfeat.o natfeats.o \
	 pmmu030.o 68040_pmmu.o amiga.o amiga2.o aros.o aros2.o delayasm.o \
	 nova.o

obj-$(ARCH_COLDFIRE) += coldfire.o coldfire2.o spi.o

obj-$(ARCH_ARM) += vectorsasm.o aciaemu.o

# The cache maintenance operations below do not exist on the ARM1176 used
# by the first generation Raspberry Pi.
obj-$(CPU_ARMV7) += cache_armv7.o cache_armv7_asm.o

obj-$(MACHINE_RPI) += raspi_board.o raspi_uart.o raspi_int.o raspi_mbox.o \
	 raspi_screen.o raspi_emmc.o

obj-$(MACHINE_VIRT_ARM) += virt_uart.o

# Fonts.  A multi-language image needs the fonts of every charset, a
# single-country one only those of its own charset.  See country.mk.
obj-y += $(FONTOBJ)
