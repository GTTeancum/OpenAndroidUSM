// Export code/data references to explicitly requested addresses.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class ExportAddressReferences extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output file and one or more addresses");
        }

        try (BufferedWriter writer =
                 new BufferedWriter(new FileWriter(new File(arguments[0])))) {
            for (int index = 1; index < arguments.length; ++index) {
                Address address = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(arguments[index]);
                writer.write(address + "\n");
                ReferenceIterator references = currentProgram
                    .getReferenceManager().getReferencesTo(address);
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Function function = currentProgram.getFunctionManager()
                        .getFunctionContaining(reference.getFromAddress());
                    writer.write("  " + reference.getFromAddress() + " " +
                        reference.getReferenceType() + " " +
                        (function == null ? "<data>" :
                         function.getName(true)) + "\n");
                }
            }
        }
    }
}
