# Resident Decompiled Overlays

Resident native overlays compiled directly from the matching Age of Empires II PS2 decompilation repository (`samuelcaldas/age-of-empires-2-decomp`, commit `b1b7c5ec9406c83304f12102afff98f8fed7b45e`).

Replaces the 12,000 statically generated recompilation files for the 8 disc overlays (`DCOMP_CD.BIN`, `HIST_CD.BIN`, `LOBBY_CD.BIN`, `MO_OV_CD.BIN`, `MULTIMCD.BIN`, `RMMMEMCD.BIN`, `SMENU_CD.BIN`, `THELP_CD.BIN`).
All overlays remain resident in host memory and dispatch through `PS2Runtime`'s overlay region table based on the active overlay ID.
