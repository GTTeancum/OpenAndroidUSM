// Export code/data references to named symbols.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class ExportSymbolReferences extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output file and name fragments");
        }
        List<String> fragments = new ArrayList<>();
        for (int index = 1; index < arguments.length; ++index) {
            fragments.add(arguments[index]);
        }
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(new File(arguments[0])))) {
            SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
            while (symbols.hasNext() && !monitor.isCancelled()) {
                Symbol symbol = symbols.next();
                String name = symbol.getName(true);
                if (fragments.stream().noneMatch(name::contains)) {
                    continue;
                }
                writer.write(name + " @ " + symbol.getAddress() + "\n");
                ReferenceIterator references = currentProgram.getReferenceManager()
                    .getReferencesTo(symbol.getAddress());
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Function function = currentProgram.getFunctionManager()
                        .getFunctionContaining(reference.getFromAddress());
                    writer.write("  " + reference.getFromAddress() + " " +
                        reference.getReferenceType() + " " +
                        (function == null ? "<data>" : function.getName(true)) + "\n");
                }
            }
        }
    }
}
