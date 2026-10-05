/* GCC-only capacity check; MDK uses its IROM configuration. */
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 352K
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))
    *(.text*) *(.rodata*) *(.init*) *(.fini*)
    *(.ARM.extab*) *(.ARM.exidx*)
  } > FLASH
  .data :
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*) *(COMMON)
    _ebss = .;
  } > RAM
  end = _ebss;
  /DISCARD/ : { *(.comment*) *(.note*) }
}
