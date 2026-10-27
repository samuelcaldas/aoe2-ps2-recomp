// Integration regression on an analysed program; run with -readOnly -noanalysis.
// Verifies repeatability and preservation of pre-existing function bodies.
// @category AOE2.Tests
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;

import java.util.HashMap;
import java.util.Map;

public class TestReferencedFunctionDiscovery extends GhidraScript {
    @Override
    protected void run() throws Exception {
        Map<Address, AddressSet> before = snapshot();
        runScript("DiscoverReferencedFunctions.java");
        assertPreserved(before);
        Map<Address, AddressSet> after = snapshot();
        runScript("DiscoverReferencedFunctions.java");
        assertPreserved(after);
        if (snapshot().size() != after.size()) {
            throw new AssertionError("discovery is not idempotent");
        }
        println("Discovery preservation and idempotence assertions passed");
    }

    private Map<Address, AddressSet> snapshot() {
        Map<Address, AddressSet> bodies = new HashMap<>();
        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            bodies.put(function.getEntryPoint(), new AddressSet(function.getBody()));
        }
        return bodies;
    }

    private void assertPreserved(Map<Address, AddressSet> expected) {
        for (Map.Entry<Address, AddressSet> entry : expected.entrySet()) {
            Function function = getFunctionAt(entry.getKey());
            if (function == null || !entry.getValue().hasSameAddresses(function.getBody())) {
                throw new AssertionError("existing function changed at " + entry.getKey()
                        + " before=" + entry.getValue() + " after="
                        + (function == null ? "missing" : function.getBody()));
            }
        }
    }
}
