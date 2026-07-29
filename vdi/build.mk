#
# vdi/build.mk - objects making up the VDI
#

obj-y += vdi_entry.o vdi_bezier.o vdi_col.o vdi_control.o vdi_esc.o \
	 vdi_fill.o vdi_gdp.o vdi_input.o vdi_line.o vdi_main.o \
	 vdi_marker.o vdi_misc.o vdi_mouse.o vdi_raster.o vdi_text.o \
	 vdi_textblit.o

obj-$(MACHINE_RPI) += raspi_mouse.o

obj-y += vdi_tblit.o

# The hand written blitter code is only valid on a real 68000 family CPU.
obj-$(ARCH_M68K_CLASSIC) += vdi_blit.o

# This one must be the last VDI object.
obj-y += endvdi.o
