// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(__x86_64__)
// Shared objects do not receive the executable startup note. Publish the
// GNU_PROPERTY_X86_ISA_1_NEEDED property (0xc0008002) with the baseline bit.
__asm__(".pushsection .note.gnu.property,\"a\"\n"
        ".p2align 3\n"
        ".long 4\n"
        ".long 16\n"
        ".long 5\n"
        ".asciz \"GNU\"\n"
        ".p2align 3\n"
        ".long 0xc0008002\n"
        ".long 4\n"
        ".long 1\n"
        ".long 0\n"
        ".popsection\n");
#endif
