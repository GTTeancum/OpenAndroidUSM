// Export reviewed seed functions as Ghidra C pseudocode for manual reconstruction.
// @category OpenAndroidUSM

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class ExportSelectedDecompilation extends GhidraScript {
    private static String safeFileName(String value) {
        String safe = value.replaceAll("[^A-Za-z0-9_.-]", "_");
        // Template-heavy DWARF names can exceed the Windows path limit. The
        // entry address remains the stable unique prefix for a truncated name.
        return safe.length() <= 96 ? safe : safe.substring(0, 96);
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output directory followed by one or more name fragments");
        }

        File outputDirectory = new File(arguments[0]);
        if (!outputDirectory.exists() && !outputDirectory.mkdirs()) {
            throw new IOException("Could not create output directory: " + outputDirectory);
        }

        List<String> fragments = new ArrayList<>();
        for (int index = 1; index < arguments.length; index++) {
            fragments.add(arguments[index]);
        }

        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        decompiler.setOptions(options);
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Decompiler could not open the program");
        }

        int count = 0;
        try {
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                String qualifiedName = function.getName(true);
                boolean selected = fragments.stream().anyMatch(qualifiedName::contains);
                if (!selected) {
                    continue;
                }

                DecompileResults results = decompiler.decompileFunction(function, 120, monitor);
                if (!results.decompileCompleted() || results.getDecompiledFunction() == null) {
                    printerr("Failed to decompile " + qualifiedName + ": " + results.getErrorMessage());
                    continue;
                }

                String fileName = function.getEntryPoint() + "_" + safeFileName(qualifiedName) + ".cpp";
                File output = new File(outputDirectory, fileName);
                try (BufferedWriter writer = new BufferedWriter(new FileWriter(output))) {
                    writer.write("// Binary address: 0x" + function.getEntryPoint() + "\n");
                    writer.write("// Preserved symbol: " + qualifiedName + "\n\n");
                    writer.write(results.getDecompiledFunction().getC());
                    writer.write("\n");
                }
                count++;
            }
        }
        finally {
            decompiler.dispose();
        }
        println("Exported " + count + " selected decompilations to " + outputDirectory.getAbsolutePath());
    }
}
