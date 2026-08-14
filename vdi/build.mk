#
# vdi/build.mk - objects making up the VDI
#

obj-y += vdi_entry.o vdi_bezier.o vdi_col.o vdi_control.o vdi_esc.o \
	 vdi_fill.o vdi_gdp.o vdi_input.o vdi_line.o vdi_main.o \
	 vdi_marker.o vdi_misc.o vdi_mouse.o vdi_raster.o vdi_text.o \
	 vdi_textblit.o

# The dispatch machinery (selection + the per-renderer ops tables) only
# exists when more than one renderer is enabled: with exactly one, the
# callers call that renderer's primitives directly (see vdi_misc.c/
# vdi_fill.c/vdi_line.c) and nothing dispatches through these tables.
obj-$(CONF_WITH_VDI_BACKEND_DISPATCH) += vdi_backend.o vdi_backend_planar.o
obj-$(CONF_WITH_VDI_BACKEND_TRUECOLOR) += vdi_backend_truecolor.o
obj-$(CONF_WITH_VDI_BACKEND_TRUECOLOR32) += vdi_backend_truecolor32.o

obj-$(MACHINE_RPI) += raspi_mouse.o

obj-y += vdi_tblit.o

# The hand written blitter code is only valid on a real 68000 family CPU.
obj-$(ARCH_M68K_CLASSIC) += vdi_blit.o

# This one must be the last VDI object.
obj-y += endvdi.o
