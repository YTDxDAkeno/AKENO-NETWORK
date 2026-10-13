/*
 * Embeds a file into the binary at build time with the assembler's .incbin directive (supported
 * by GNU as and LLVM's integrated assembler, so it works for the Linux and PS5 builds alike).
 * The data is followed by a NUL byte; name_end points just past that NUL.
 *
 *   AKENO_EMBED_FILE(akeno_ui_page, "/abs/path/index.html")
 *   extern const char akeno_ui_page[], akeno_ui_page_end[];
 */
#ifndef AKENO_EMBED_H
#define AKENO_EMBED_H

#define AKENO_EMBED_FILE(name, path)                 \
    __asm__(".section .rodata\n"                    \
            ".balign 16\n"                          \
            ".global " #name "\n"                   \
            ".type " #name ", @object\n"            \
            #name ":\n"                             \
            ".incbin \"" path "\"\n"                \
            ".byte 0\n"                             \
            ".global " #name "_end\n"               \
            ".type " #name "_end, @object\n"        \
            #name "_end:\n"                         \
            ".previous\n");                         \
    extern const char name[];                       \
    extern const char name##_end[]

#endif
