/* Default linker script, for normal executables */
/* Copyright (C) 2020 Free Software Foundation, Inc.
   Copying and distribution of this script, with or without modification,
   are permitted in any medium without royalty provided the copyright
   notice and this notice are preserved.  */
OUTPUT_FORMAT("elf32-mrisc32")
OUTPUT_ARCH(mrisc32)
ENTRY(_start)
SEARCH_DIR("/builds/mrisc32/mrisc32-gnu-toolchain/mrisc32-gnu-toolchain/mrisc32-elf/lib");
SECTIONS
{
  /* Read-only sections first.  */
  . = 0x00000200;
  .text :
  {
    *(.text.entry)
    *(.text.start)
    *(.text.D_PolysetDrawSpans8)
    *(.text.D_DrawSpans16)
    *(.text.D_DrawZSpans)
    *(.text.D_PolysetScanLeftEdge)
    *(.text.R_GenerateSpans.lto_priv.0)
    *(.text.ceilf)
    *(.text.R_AliasVertsVec)
    *(.text.D_PolysetDraw)
    *(.text.D_PolysetSetUpForLineScan)
    *(.text.R_ClipEdge)
    *(.text.D_PolysetCalcGradients)
    *(.text.D_DrawNonSubdiv)
    *(.text.R_DrawSurfaceBlock8_mip0.lto_priv.0)
    *(.text.R_DrawSurfaceBlock8_mip1.lto_priv.0)
    *(.text.R_ScanEdges)
    *(.text.R_RenderFace)
    *(.text.D_PolysetRecursiveTriangle)
    *(.text.floorf)
    *(.text.R_RecursiveWorldNode.lto_priv.0)
    *(.text.D_Sky_uv_To_st)
    *(.text.R_AliasDrawModel)
    *(.text.Draw_TransPicCropped)
    *(.text.D_CalcGradients)
    *(.text.RecursiveLightPoint)
    *(.text.R_DrawSurfaceBlock8_mip2.lto_priv.0)
    *(.text.cosf)
    *(.text.Mod_Extradata)
    *(.text._localeconv_r)
    *(.text._read)
    *(.text._write)

    *(.text*)
    KEEP (*(SORT_NONE(.init)))
                 KEEP (*(SORT_NONE(.fini)))
  }
  . = ALIGN(32);
  .rodata :
  {
    *(.rodata*)
  }
  . = ALIGN(32);
  .ctor :
  {
    __CTOR_START = .;
    /* The compiler uses crtbegin.o to find the start
       of the constructors, so we make sure it is
       first.  Because this is a wildcard, it
       doesn't matter if the user does not
       actually link against crtbegin.o; the
       linker won't look for a file to match a
       wildcard.  The wildcard also means that it
       doesn't matter which directory crtbegin.o
       is in.  */
    KEEP (*crtbegin.o(.ctors))
    KEEP (*crtbegin?.o(.ctors))
    /* We don't want to include the .ctor section from
       the crtend.o file until after the sorted ctors.
       The .ctor section from the crtend file contains the
       end of ctors marker and it must be last */
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o) .ctors))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    __CTOR_END = .;
  }
  . = ALIGN(32);
  .dtor :
  {
    __DTOR_START = .;
    KEEP (*crtbegin.o(.dtors))
    KEEP (*crtbegin?.o(.dtors))
    KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o) .dtors))
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
    __DTOR_END = .;
  }
  . = ALIGN(32);
  .eh_frame   :
  {
    KEEP (*(.eh_frame))*(.eh_frame.*)
  }
  . = ALIGN(32);
  /* Read/write sections.  */
  . = .;
  . = ALIGN(32);
  __data_start = .;
  .data :
  {
    *(.data*)
  }
  __data_end = .;
  . = ALIGN(32);
  __bss_start = .;
  .bss (NOLOAD) :
  {
    *(.bss*)
    *(COMMON)
  }
  . = ALIGN(32);
  __bss_end = .;
  /* End of all section: This is normally where the heap starts.  */
  _end = .;
}

