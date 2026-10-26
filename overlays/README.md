# Decompiled overlay sources (not built)

C sources for the 8 disc overlays imported from the owner's decompilation repository
(`samuelcaldas/age-of-empires-2-decomp`, commit `b1b7c5ec9406c83304f12102afff98f8fed7b45e`).

They are **not compiled into the game**. The C was translated from a wrong-ISA disassembly (EE MMI instructions
left unhandled), dereferences guest addresses as host pointers and never sets `gp`, so it cannot run. The game
links the recompiled overlays from `generated/overlays/` instead; see `docs/sot/overlay-recompilation.md`.
