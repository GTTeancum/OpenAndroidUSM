// Export the analyzed function database to a stable CSV file.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;

public class ExportFunctionInventory extends GhidraScript {
    private static String csv(String value) {
        if (value == null) {
            return "";
        }
        return "\"" + value.replace("\"", "\"\"") + "\"";
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length != 1) {
            throw new IllegalArgumentException("Expected one argument: output CSV path");
        }

        File output = new File(arguments[0]);
        File parent = output.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("Could not create output directory: " + parent);
        }

        int count = 0;
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(output))) {
            writer.write("address,size,name,namespace,signature,calling_convention,thunk,external,source\n");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                writer.write(csv(function.getEntryPoint().toString()));
                writer.write("," + function.getBody().getNumAddresses());
                writer.write("," + csv(function.getName()));
                writer.write("," + csv(function.getParentNamespace().getName(true)));
                writer.write("," + csv(function.getSignature().getPrototypeString()));
                writer.write("," + csv(function.getCallingConventionName()));
                writer.write("," + function.isThunk());
                writer.write("," + function.isExternal());
                writer.write("," + csv(function.getSymbol().getSource().toString()));
                writer.write("\n");
                count++;
            }
        }
        println("Exported " + count + " functions to " + output.getAbsolutePath());
    }
}

