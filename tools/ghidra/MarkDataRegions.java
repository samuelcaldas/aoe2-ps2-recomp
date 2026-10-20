// Marks large return-free regions of executable memory as non-executable data.
//
// Stripped PS2 ELFs merge code and data into one RWX segment, so exporters that
// only look at block permissions treat data as code. Real MIPS code reaches a
// `jr $ra` return every few KiB; a region with no return for longer than the
// threshold is data. Such regions are split into their own memory block, made
// non-executable, and their listing and functions are cleared.
//
// Region edges are found from raw words, not from Ghidra's listing: the generic
// MIPS64 language does not decode R5900 instructions such as LQ/SQ, so code next
// to data is often left undisassembled. From each edge the scan walks toward the
// data and stops at the first certain data word; without one, the uncertain
// window is kept as code so real functions are never truncated.
//
// Script arguments: [maxReturnGapBytes] (default 65536).
// Must run after auto-analysis and before the function exporter.
// @category AOE2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

import java.util.ArrayList;
import java.util.List;

public class MarkDataRegions extends GhidraScript {

    private static final long DEFAULT_MAX_RETURN_GAP_BYTES = 64L * 1024L;
    private static final int MAX_UNCERTAIN_EDGE_BYTES = 4096;
    private static final int JR_RA_WORD = 0x03E00008;
    private static final int INSTRUCTION_BYTES = 4;
    private static final int RETURN_SEQUENCE_BYTES = 8;
    private static final int OPCODE_SPECIAL = 0x00;
    private static final int FUNCT_TGE = 0x30;
    private static final int FUNCT_TLTU = 0x33;
    private static final int FIRST_PRINTABLE = 0x20;
    private static final int LAST_PRINTABLE = 0x7E;

    @Override
    public void run() throws Exception {
        long maxReturnGapBytes = parseMaxReturnGap(getScriptArgs());
        List<AddressSet> regions = new ArrayList<>();
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (block.isExecute() && block.isInitialized()) {
                regions.addAll(findDataRegions(block, maxReturnGapBytes));
            }
        }
        long markedBytes = 0;
        for (AddressSet region : regions) {
            markAsData(region);
            markedBytes += region.getNumAddresses();
        }
        println(String.format("MarkDataRegions: marked %d region(s), %d bytes, as non-executable data (threshold %d bytes)",
            regions.size(), markedBytes, maxReturnGapBytes));
    }

    private static long parseMaxReturnGap(String[] arguments) {
        if (arguments.length == 0) {
            return DEFAULT_MAX_RETURN_GAP_BYTES;
        }
        long value = Long.decode(arguments[0]);
        if (value <= 0) {
            throw new IllegalArgumentException("maxReturnGapBytes must be positive: " + arguments[0]);
        }
        return value;
    }

    private List<AddressSet> findDataRegions(MemoryBlock block, long maxReturnGapBytes) throws Exception {
        int[] words = readWords(block);
        List<AddressSet> regions = new ArrayList<>();
        int codeEnd = 0;
        for (int index = 0; index <= words.length; index++) {
            boolean atEnd = index == words.length;
            if (!atEnd && words[index] != JR_RA_WORD) {
                continue;
            }
            int gapEnd = atEnd ? words.length : index;
            if ((long) (gapEnd - codeEnd) * INSTRUCTION_BYTES > maxReturnGapBytes) {
                int dataStart = dataStartAfter(words, codeEnd, gapEnd);
                int dataEnd = atEnd ? words.length : codeStartBefore(words, gapEnd, dataStart);
                addRegion(regions, block, dataStart, dataEnd);
            }
            codeEnd = index + RETURN_SEQUENCE_BYTES / INSTRUCTION_BYTES;
        }
        return regions;
    }

    private static int[] readWords(MemoryBlock block) throws Exception {
        byte[] bytes = new byte[(int) block.getSize()];
        block.getBytes(block.getStart(), bytes);
        int[] words = new int[bytes.length / INSTRUCTION_BYTES];
        for (int index = 0; index < words.length; index++) {
            int offset = index * INSTRUCTION_BYTES;
            words[index] = (bytes[offset] & 0xFF) | (bytes[offset + 1] & 0xFF) << 8 |
                           (bytes[offset + 2] & 0xFF) << 16 | (bytes[offset + 3] & 0xFF) << 24;
        }
        return words;
    }

    private static int dataStartAfter(int[] words, int codeEnd, int limit) {
        int windowEnd = Math.min(limit, codeEnd + MAX_UNCERTAIN_EDGE_BYTES / INSTRUCTION_BYTES);
        for (int index = codeEnd; index < windowEnd; index++) {
            if (isCertainData(words, index, index + 1)) {
                return index;
            }
        }
        return windowEnd;
    }

    private static int codeStartBefore(int[] words, int returnIndex, int floor) {
        int windowStart = Math.max(floor, returnIndex - MAX_UNCERTAIN_EDGE_BYTES / INSTRUCTION_BYTES);
        for (int index = returnIndex - 1; index >= windowStart; index--) {
            if (isCertainData(words, index, index - 1)) {
                return index + 1;
            }
        }
        return windowStart;
    }

    // Trap-on-compare instructions are never emitted by the compiler, and two
    // consecutive all-printable words are text; either one marks data.
    private static boolean isCertainData(int[] words, int index, int neighborIndex) {
        if (isCompareTrap(words[index])) {
            return true;
        }
        boolean hasNeighbor = neighborIndex >= 0 && neighborIndex < words.length;
        return hasNeighbor && isPrintableText(words[index]) && isPrintableText(words[neighborIndex]);
    }

    private static boolean isCompareTrap(int word) {
        int funct = word & 0x3F;
        return (word >>> 26) == OPCODE_SPECIAL && funct >= FUNCT_TGE && funct <= FUNCT_TLTU;
    }

    private static boolean isPrintableText(int word) {
        for (int shift = 0; shift < 32; shift += 8) {
            int character = (word >>> shift) & 0xFF;
            if (character < FIRST_PRINTABLE || character > LAST_PRINTABLE) {
                return false;
            }
        }
        return true;
    }

    private static void addRegion(List<AddressSet> regions, MemoryBlock block, int startIndex, int endIndex) {
        if (endIndex <= startIndex) {
            return;
        }
        Address start = block.getStart().add((long) startIndex * INSTRUCTION_BYTES);
        Address end = block.getStart().add((long) endIndex * INSTRUCTION_BYTES - 1);
        regions.add(new AddressSet(start, end));
    }

    private void markAsData(AddressSet region) throws Exception {
        FunctionManager functionManager = currentProgram.getFunctionManager();
        List<Address> entries = new ArrayList<>();
        for (Function function : functionManager.getFunctions(region, true)) {
            entries.add(function.getEntryPoint());
        }
        for (Address entry : entries) {
            functionManager.removeFunction(entry);
        }
        clearListing(region);
        isolateBlock(region.getMinAddress(), region.getMaxAddress()).setExecute(false);
    }

    private MemoryBlock isolateBlock(Address start, Address last) throws Exception {
        Memory memory = currentProgram.getMemory();
        MemoryBlock block = memory.getBlock(start);
        if (!block.getStart().equals(start)) {
            memory.split(block, start);
            block = memory.getBlock(start);
        }
        if (block.getEnd().compareTo(last) > 0) {
            memory.split(block, last.add(1));
            block = memory.getBlock(start);
        }
        return block;
    }
}
