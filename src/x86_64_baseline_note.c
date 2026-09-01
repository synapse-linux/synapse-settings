// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(__x86_64__)
// Publish explicit GNU_PROPERTY_X86_ISA_1_NEEDED and ISA_1_USED baseline
// properties. The companion linker script replaces executable startup notes
// without relying on the unsafe linker -z x86-64-baseline path.
__asm__(".pushsection .note.synapse.gnu.property,\"a\",@note\n"
        ".p2align 3\n"
        ".long 4\n"
        ".long 32\n"
        ".long 5\n"
        ".asciz \"GNU\"\n"
        ".p2align 3\n"
        ".long 0xc0008002\n"
        ".long 4\n"
        ".long 1\n"
        ".long 0\n"
        ".long 0xc0010002\n"
        ".long 4\n"
        ".long 1\n"
        ".long 0\n"
        ".popsection\n");
#endif
