// X-macro map for kt_libcdvd_server module functions.
// Entries are limited to starts exported as functions by the authoritative Ghidra map.
ENTRY_R(Init, 0x001295D0, m.Reset())
ENTRY_V(ProcessRequests, 0x00129B10, m.ProcessPendingRequests())
ENTRY_R(MaskCmd, 0x0012CB00, (U(0) & 0x7u))
ENTRY_R(CheckFlagBit15, 0x0012CB08, (U(0) & 0x8000u))
ENTRY_R(ClearFlagBit15, 0x0012CB10, (U(0) & ~0x8000u))
