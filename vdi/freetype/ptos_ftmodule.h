/*
 * ptos_ftmodule.h - the FreeType modules pTOS builds
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Selected with FT_CONFIG_MODULES_H (see vdi/freetype/freetype.mk) in
 * place of FreeType's own list.  TrueType and OpenType (CFF) fonts, the
 * PostScript helpers the CFF driver needs, and the monochrome renderer:
 * the VDI draws 1-bit glyphs.  Keep it in step with FREETYPE_SRC there.
 */

FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
