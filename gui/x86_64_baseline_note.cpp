// SPDX-License-Identifier: MIT

#if defined(__x86_64__)
// Shared objects do not receive the executable startup note. Publish explicit
// GNU_PROPERTY_X86_ISA_1_NEEDED (0xc0008002) and ISA_1_USED (0xc0010002)
// properties with the baseline bit.
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
