// Layout-only 32-bit host compilation does not link libc. The host image
// lacks glibc-multilib's generated stubs header, whose contents only describe
// unavailable libc entrypoints and do not affect C type layout.
