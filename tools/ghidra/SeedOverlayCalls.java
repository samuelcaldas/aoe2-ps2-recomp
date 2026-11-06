// Recover main-image entries reached by direct calls from verified disc overlays.
// Candidates are filtered after data-region marking; do not split existing functions.
// @category AOE2
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.util.Iterator;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import java.nio.file.Files;
import java.nio.file.Path;

public class SeedOverlayCalls extends GhidraScript {
    @Override
    protected void run() throws Exception {
        int created = 0;
        for (String line : Files.readAllLines(Path.of(getScriptArgs()[0]))) {
            monitor.checkCancelled();
            Address address = toAddr(Long.parseUnsignedLong(line.replaceFirst("^0[xX]", ""), 16));
            MemoryBlock block = currentProgram.getMemory().getBlock(address);
            if ((address.getOffset() & 3) != 0 || block == null || !block.isInitialized() || !block.isExecute()
                    || getDataContaining(address) != null || getFunctionContaining(address) != null) {
                continue;
            }
            Instruction previous = getInstructionAt(address.subtract(4));
            if (previous != null && previous.getDelaySlotDepth() > 0) {
                continue;
            }
            // Check the raw predecessor too, including when it is not disassembled.
            if (address.subtract(4).compareTo(block.getStart()) >= 0) {
                int word = currentProgram.getMemory().getInt(address.subtract(4));
                int op = word >>> 26;
                if (op == 1 || (op >= 2 && op <= 7) || (op >= 0x14 && op <= 0x17)
                        || (op == 0 && ((word & 63) == 8 || (word & 63) == 9))
                        || (op >= 0x10 && op <= 0x12 && ((word >>> 21) & 31) == 8)) {
                    continue;
                }
            }
            if ((getInstructionAt(address) != null || disassemble(address))
                    && getInstructionAt(address) != null && !getInstructionAt(address).isInDelaySlot()
                     ) {
                AddressSet body = new AddressSet(CreateFunctionCmd.getFunctionBody(currentProgram, address, monitor));
                body.add(address);
                Iterator<Function> owners = currentProgram.getFunctionManager().getFunctionsOverlapping(body);
                while (owners.hasNext()) {
                    body.delete(owners.next().getBody());
                }
                currentProgram.getFunctionManager().createFunction(null, address, body, SourceType.DEFAULT);
                created++;
            }
        }
        println("Recovered overlay-called main entries: " + created);
    }
}
