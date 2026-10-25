// X-macro map for kt_hd module functions.
ENTRY_R(TableLookup, 0x00129588, m.TableLookup(U(0), I(1), I(2)))
ENTRY_R(SetFlag, 0x00128F40, m.SetFlag(I(0)))
ENTRY_R(GetFlag, 0x00128F68, m.GetFlag())
ENTRY_R(ReadAndClearReg, 0x00129548, m.ReadAndClearRegister())
ENTRY_R(ExecuteCallback, 0x00129560, m.ExecuteCallback(U(0), U(1)))
ENTRY_R(SemaStatus1, 0x00129428, 1)
ENTRY_R(SemaStatus2, 0x001294C0, 1)
