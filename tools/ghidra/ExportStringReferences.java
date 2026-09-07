// Export defined strings and their code/data references by text fragment.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class ExportStringReferences extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output file and one or more text fragments");
        }

        List<String> fragments = new ArrayList<>();
        for (int index = 1; index < arguments.length; ++index) {
            fragments.add(arguments[index].toLowerCase());
        }

        try (BufferedWriter writer = new BufferedWriter(
                 new FileWriter(new File(arguments[0])))) {
            DataIterator dataItems = currentProgram.getListing()
                .getDefinedData(true);
            while (dataItems.hasNext() && !monitor.isCancelled()) {
                Data data = dataItems.next();
                Object value = data.getValue();
                if (!(value instanceof String)) {
                    continue;
                }
                String stringValue = (String)value;
                String lowerValue = stringValue.toLowerCase();
                if (fragments.stream().noneMatch(lowerValue::contains)) {
                    continue;
                }

                writer.write(data.getAddress() + " \"" +
                    stringValue.replace("\n", "\\n") + "\"\n");
                ReferenceIterator references = currentProgram
                    .getReferenceManager().getReferencesTo(data.getAddress());
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
