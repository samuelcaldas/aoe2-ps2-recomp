// Integration assertion for an already analysed, user-owned program.
// Pass one or more expected entry addresses; run read-only before/after discovery.
// @category AOE2.Tests
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

public class AssertFunctionEntries extends GhidraScript {
    @Override
    protected void run() throws Exception {
        if (getScriptArgs().length == 0) {
            throw new IllegalArgumentException("expected at least one entry address");
        }
        for (String argument : getScriptArgs()) {
            Address address = toAddr(Long.decode(argument));
            if (getFunctionAt(address) == null) {
                throw new AssertionError("missing function entry " + address);
            }
        }
        println("Function entry assertions passed");
    }
}
