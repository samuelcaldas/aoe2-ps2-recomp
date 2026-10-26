// X-macro map for kt_libcdvd_server module functions.
// Entries are limited to starts exported as functions by the authoritative Ghidra map.
DEFERRED(0x001295D0, "Returns the accepted-media mask for the requested mode; resetting an isolated read model does not implement this contract.")
DEFERRED(0x00129B10, "Media classification returns a result and writes the caller's structure; the isolated read queue implements neither contract.")
DEFERRED(0x0012BC20, "The SCmd semaphore getter does not complete pending guest work; completion belongs to the original CDVD worker.")
ENTRY_R(MaskCmd, 0x0012CB00, (U(0) & 0x7u))
ENTRY_R(CheckFlagBit15, 0x0012CB08, (U(0) & 0x8000u))
ENTRY_R(ClearFlagBit15, 0x0012CB10, (U(0) & ~0x8000u))
