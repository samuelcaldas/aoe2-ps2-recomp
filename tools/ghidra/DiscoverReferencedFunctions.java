// Recover orphan instruction entries referenced by data (e.g. callback tables).
// Run after data-region marking; never disassemble data or change an existing function's body.
// @category AOE2
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

public class DiscoverReferencedFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        List<Address> candidates = new ArrayList<>();
        for (Instruction instruction : currentProgram.getListing().getInstructions(true)) {
            monitor.checkCancelled();
            if (isCandidate(instruction)) {
                candidates.add(instruction.getAddress());
            }
        }
        int created = createEntries(candidates);
        println("Recovered data-referenced function entries: " + created);
        int pointed = createEntries(undisassembledPointerTargets());
        println("Recovered pointer-table function entries: " + pointed);
    }

    /**
     * Code that only a pointer table names and Ghidra never disassembled (adjustor thunks): no
     * reference exists, so the pass above cannot see it. A stored word is taken as an entry only
     * where control cannot fall into it, so constants that happen to equal a code address mid-flow
     * are ignored.
     */
    private List<Address> undisassembledPointerTargets() throws Exception {
        List<Address> targets = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized()) {
                continue;
            }
            for (Address source = block.getStart(); source.compareTo(block.getEnd().subtract(3)) <= 0; source = source.add(4)) {
                monitor.checkCancelled();
                Address target = storedCodeAddress(source);
                if (target != null && disassemble(target) && getInstructionAt(target) != null) {
                    targets.add(target);
                }
            }
        }
        return targets;
    }

    private Address storedCodeAddress(Address source) throws Exception {
        if (getInstructionContaining(source) != null) {
            return null;
        }
        long value = Integer.toUnsignedLong(currentProgram.getMemory().getInt(source));
        if (value % 4 != 0 || value < 8) {
            return null;
        }
        Address target = toAddr(value);
        MemoryBlock block = currentProgram.getMemory().getBlock(target);
        if (block == null || !block.isExecute() || !block.isInitialized() || getInstructionAt(target) != null) {
            return null;
        }
        if (getDataContaining(target) != null || getFunctionContaining(target) != null) {
            return null;
        }
        return controlCannotFallInto(target) ? target : null;
    }

    /** Preceded by padding, or by the delay slot of a jump that never returns here (j, jr). */
    private boolean controlCannotFallInto(Address target) throws Exception {
        long previous = word(target.subtract(4));
        long beforePrevious = word(target.subtract(8));
        if (previous == 0 && beforePrevious == 0) {
            return true;
        }
        boolean jump = beforePrevious >>> 26 == 2;
        boolean registerJump = beforePrevious >>> 26 == 0 && (beforePrevious & 0x3F) == 8;
        return jump || registerJump;
    }

    private long word(Address address) throws Exception {
        if (!currentProgram.getMemory().getLoadedAndInitializedAddressSet().contains(address, address.add(3))) {
            return -1;
        }
        return Integer.toUnsignedLong(currentProgram.getMemory().getInt(address));
    }

    private boolean isCandidate(Instruction instruction) {
        Address address = instruction.getAddress();
        MemoryBlock block = currentProgram.getMemory().getBlock(address);
        if (block == null || !block.isExecute() || instruction.isInDelaySlot()) {
            return false;
        }
        if (address.getOffset() % 4 != 0 || getFunctionContaining(address) != null) {
            return false;
        }
        return hasDataReference(address);
    }

    private boolean hasDataReference(Address address) {
        for (Reference reference : getReferencesTo(address)) {
            if (reference.getReferenceType().isData()
                    && getDataContaining(reference.getFromAddress()) != null) {
                return true;
            }
        }
        return false;
    }

    private int createEntries(List<Address> candidates) throws Exception {
        int created = 0;
        for (Address address : candidates) {
            monitor.checkCancelled();
            if (getFunctionContaining(address) != null) {
                continue;
            }
            currentProgram.getFunctionManager().createFunction(null, address, unownedBody(address), SourceType.DEFAULT);
            created++;
        }
        return created;
    }

    /**
     * The flow body from the entry minus code other functions already own. Letting Ghidra take
     * a shared tail moves it out of its owner; the exporter then shrinks the owner's range and
     * code that only that range covered (switch cases reached through a jump table) is lost.
     */
    private AddressSetView unownedBody(Address entry) throws Exception {
        AddressSet body = new AddressSet(CreateFunctionCmd.getFunctionBody(currentProgram, entry, monitor));
        body.add(entry);
        Iterator<Function> owners = currentProgram.getFunctionManager().getFunctionsOverlapping(body);
        while (owners.hasNext()) {
            body.delete(owners.next().getBody());
        }
        return body;
    }
}
