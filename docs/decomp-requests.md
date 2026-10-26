# Requests and divergence reports to the decomp session

Requests sent to the owner's decomp session (`aoe-decomp`, repository `../aoe`), and every place where its code
disagrees with the binary. The binary and the generated code win; decomp code is ported only after it has been
compared with the generated unit. No disassembly here: function, file and status only.

| Date | Kind | Decomp file / function | What the binary does | Status |
|------|------|------------------------|----------------------|--------|
| 2026-10-26 | Divergence | `src/app/tribe_app.cpp` / `func_00185230` | Does not call the main-menu overlay loader. It calls the message-pump helpers and makes indirect calls. | Reported |
| 2026-10-26 | Request | Screen-manager functions that open the main menu (callers of the main-menu loader) | Needed to find the predicate that never opens the menu | Pending |
| 2026-10-26 | Divergence | `docs/ps2_binary_analysis.md` §6.188 "Tactical AI destructors" (and the code around it) | This range is the memory card screen and its dialog (the code references the "Memorycard Screen" and "MemorycardScreenDialog" panels) | Reported |
