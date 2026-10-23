// Creates functions at the addresses listed in a file, one hex address per line.
//
// A raw overlay import has no symbols, so entry points that only the main program calls are
// never discovered. Each address is disassembled and made a function if none exists there.
// Script arguments: <seed file>. Must run before the function exporter.
// @category AOE2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.File;
import java.nio.file.Files;

public class SeedFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            throw new IllegalArgumentException("usage: SeedFunctions.java <seed file>");
        }
        for (String line : Files.readAllLines(new File(arguments[0]).toPath())) {
            String text = line.trim();
            if (text.isEmpty()) {
                continue;
            }
            Address address = toAddr(Long.parseUnsignedLong(text.replaceFirst("^0[xX]", ""), 16));
            disassemble(address);
            if (getFunctionAt(address) == null && createFunction(address, null) == null) {
                throw new IllegalStateException("cannot create a function at " + address);
            }
        }
    }
}
