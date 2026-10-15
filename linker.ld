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
  
  .text : ONLY {
    KEEP(*(.text.entry))
    *(.text .text.* .gnu.linkonce.t.*)
    KEEP(*(.init))
    KEEP(*(.fini))
  } : text_segment

  /* ========================================================================
     PLT and GOT sections - Part of text segment
     ======================================================================== */
  
  .plt : {
    *(.plt)
  } : text_segment
  
  .plt.got : {
    *(.plt.got)
  } : text_segment

  /* ========================================================================
     RODATA SEGMENT - Read-only data (R only, NO write/execute)
     ======================================================================== */
  
  . = ALIGN(0x1000);
  
  .rodata : ONLY {
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.eh_frame_hdr)
    *(.eh_frame)
  } : rodata_segment
  
  .data.rel.ro : ONLY {
    *(.data.rel.ro .data.rel.ro.*)
  } : rodata_segment
  
  .got : ONLY {
    *(.got)
  } : rodata_segment

  /* ========================================================================
     DYNAMIC SECTION - Dynamic linking info (goes into rodata segment)
     ======================================================================== */
  
  . = ALIGN(0x1000);
  
  .dynamic : {
    *(.dynamic)
  } : rodata_segment

  /* ========================================================================
     DATA SEGMENT - Writeable data (RW only, NO execute)
     ======================================================================== */
  
  . = ALIGN(0x1000);
  
  .data : ONLY {
    *(.data .data.* .gnu.linkonce.d.*)
    *(.init_array)
    *(.fini_array)
  } : data_segment
  
  .bss : ONLY {
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)
  } : data_segment

  /* ========================================================================
     SYMBOL TABLES AND RELOCATIONS (in data segment)
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
  
  .rela.text : {
    *(.rela.text*)
  } : data_segment
  
  .gnu.hash : {
    *(.gnu.hash)
  } : data_segment

  /* ========================================================================
     EXCEPTION HANDLING (in rodata if not already included)
     ======================================================================== */
  
  .eh_frame : {
    KEEP(*(.eh_frame))
  } : rodata_segment

  /* ========================================================================
     DEBUG SECTIONS (not loaded at runtime)
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

  /* ========================================================================
     COMMENT SECTION
     ======================================================================== */
  
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
