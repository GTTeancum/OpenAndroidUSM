// Export pointer-sized entries and resolved function names for matching data symbols.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class ExportVtableEntries extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 3) {
            throw new IllegalArgumentException(
                "Expected output file, entry count, and symbol fragments");
        }
        int entryCount = Integer.decode(arguments[1]);
        List<String> fragments = new ArrayList<>();
        for (int index = 2; index < arguments.length; ++index) {
            fragments.add(arguments[index]);
        }

        try (BufferedWriter writer =
                 new BufferedWriter(new FileWriter(new File(arguments[0])))) {
            SymbolIterator symbols = currentProgram.getSymbolTable()
                .getAllSymbols(true);
            while (symbols.hasNext() && !monitor.isCancelled()) {
                Symbol symbol = symbols.next();
                String name = symbol.getName(true);
                if (fragments.stream().noneMatch(name::contains)) {
                    continue;
                }
                Address base = symbol.getAddress();
                writer.write(name + " @ " + base + "\n");
                for (int index = 0; index < entryCount; ++index) {
                    Address slot = base.add(index * 4L);
                    int bits;
                    try {
                        bits = currentProgram.getMemory().getInt(slot);
                    } catch (Exception exception) {
                        break;
                    }
                    long unsignedBits = Integer.toUnsignedLong(bits);
                    Address target = currentProgram.getAddressFactory()
                        .getDefaultAddressSpace().getAddress(unsignedBits);
                    Function function = currentProgram.getFunctionManager()
                        .getFunctionAt(target);
                    writer.write(String.format(
                        "  +0x%03x 0x%08x %s\n", index * 4,
                        unsignedBits,
                        function == null ? "" : function.getName(true)));
                }
                writer.write("\n");
            }
        }
    }
}
