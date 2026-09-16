/* SPDX-License-Identifier: MIT */
SECTIONS
{
  .note.gnu.property :
  {
    KEEP(*(.note.synapse.gnu.property))
  }

  /DISCARD/ :
  {
    *(.note.gnu.property)
  }
}
INSERT AFTER .note.gnu.build-id;
