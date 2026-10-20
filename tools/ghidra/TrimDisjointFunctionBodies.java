// Trims function bodies whose disjoint fragments span other functions.
//
// Ghidra may attach a far-away fragment to a function body (a shared epilogue
// or a tail-jump target). Exporters that take the body's maximum address as the
// function end then emit one entry covering hundreds of unrelated functions.
// When a function's body is disjoint and its overall span contains another
// function's entry point, the body is reduced to the contiguous range that holds
// its own entry. The trimmed fragments stay reachable as their own entries or
// labels in the export.
//
// Must run after auto-analysis and before the function exporter.
// @category AOE2

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

import java.util.ArrayList;
import java.util.List;

public class TrimDisjointFunctionBodies extends GhidraScript {

    @Override
    public void run() throws Exception {
        FunctionManager functionManager = currentProgram.getFunctionManager();
        List<Function> candidates = new ArrayList<>();
        FunctionIterator functions = functionManager.getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            Function function = functions.next();
            if (spansForeignEntry(functionManager, function)) {
                candidates.add(function);
            }
        }
        long removedBytes = 0;
        for (Function function : candidates) {
            removedBytes += trimToEntryRange(function);
        }
        println(String.format("TrimDisjointFunctionBodies: trimmed %d function(s), %d bytes of foreign fragments",
            candidates.size(), removedBytes));
    }

    private static boolean spansForeignEntry(FunctionManager functionManager, Function function) {
        AddressSetView body = function.getBody();
        if (body == null || body.getNumAddressRanges() <= 1) {
            return false;
        }
        AddressSet span = new AddressSet(body.getMinAddress(), body.getMaxAddress());
        FunctionIterator inside = functionManager.getFunctions(span, true);
        while (inside.hasNext()) {
            if (!inside.next().getEntryPoint().equals(function.getEntryPoint())) {
                return true;
            }
        }
        return false;
    }

    private long trimToEntryRange(Function function) throws Exception {
        AddressSetView body = function.getBody();
        Address entry = function.getEntryPoint();
        AddressRange entryRange = body.getRangeContaining(entry);
        if (entryRange == null) {
            printerr("TrimDisjointFunctionBodies: entry outside its own body, left unchanged: " + function.getName());
            return 0;
        }
        long before = body.getNumAddresses();
        function.setBody(new AddressSet(entryRange));
        return before - entryRange.getLength();
    }
}
