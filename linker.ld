/* ============================================================================
   FIROZ OS Payload - Custom Linker Script (Fixed)
   Separates code and data into proper segments with correct flags
   ============================================================================ */

ENTRY(_start)

PHDRS {
  text_segment PT_LOAD FLAGS(5);        /* R + E (Read + Execute) */
  rodata_segment PT_LOAD FLAGS(4);      /* R (Read-only) */
  data_segment PT_LOAD FLAGS(6);        /* RW (Read + Write) */
}

SECTIONS {
  /* ========================================================================
     TEXT SEGMENT - Executable code (R E only, NO write)
     ======================================================================== */
  
  . = 0x0;
  
  .text : {
    KEEP(*(.text.entry))
    *(.text .text.* .gnu.linkonce.t.*)
    KEEP(*(.init))
    KEEP(*(.fini))
    *(.plt)
    *(.plt.got)
  } : text_segment

  /* ========================================================================
     RODATA SEGMENT - Read-only data (R only, NO write/execute)
     ======================================================================== */
  
  . = ALIGN(0x1000);
  
  .rodata : {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.eh_frame_hdr)
    *(.eh_frame)
  } : rodata_segment
  
  .data.rel.ro : {
    *(.data.rel.ro .data.rel.ro.*)
  } : rodata_segment
  
  .got : {
    *(.got)
  } : rodata_segment

  /* ========================================================================
     DYNAMIC SECTION
     ======================================================================== */
  
  . = ALIGN(0x1000);
  
  .dynamic : {
    *(.dynamic)
  } : rodata_segment

  /* ========================================================================
     DATA SEGMENT - Writeable data (RW only, NO execute)
     ======================================================================== */
  
  . = ALIGN(0x1000);
  
  .data : {
    *(.data .data.* .gnu.linkonce.d.*)
    *(.init_array)
    *(.fini_array)
  } : data_segment
  
  .bss : {
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)
  } : data_segment

  /* ========================================================================
     SYMBOL TABLES AND RELOCATIONS
     ======================================================================== */
  
  .dynsym : {
    *(.dynsym)
  } : data_segment
  
  .dynstr : {
    *(.dynstr)
  } : data_segment
  
  .rela.dyn : {
    *(.rela.dyn)
    *(.rela.plt)
  } : data_segment
  
  .gnu.hash : {
    *(.gnu.hash)
  } : data_segment

  /* ========================================================================
     DEBUG SECTIONS
     ======================================================================== */
  
  .symtab : {
    *(.symtab)
  }
  
  .strtab : {
    *(.strtab)
  }
  
  .shstrtab : {
    *(.shstrtab)
  }
  
  .comment : {
    *(.comment)
  }

  /* ========================================================================
     DISCARD UNUSED SECTIONS
     ======================================================================== */
  
  /DISCARD/ : {
    *(.note.GNU-stack)
    *(.gnu_debuglink)
    *(.gnu_debugaltlink)
    *(.note)
    *(.note.*)
  }
}
