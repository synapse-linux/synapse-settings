/* SPDX-License-Identifier: GPL-3.0-or-later */
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
