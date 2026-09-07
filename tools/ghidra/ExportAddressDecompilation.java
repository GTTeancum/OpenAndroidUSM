// Export decompilation for explicitly requested entry addresses.
// @category OpenAndroidUSM

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;

public class ExportAddressDecompilation extends GhidraScript {
    private static String safeFileName(String value) {
        String safe = value.replaceAll("[^A-Za-z0-9_.-]", "_");
        return safe.length() <= 96 ? safe : safe.substring(0, 96);
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output directory and one or more function addresses");
        }
        File outputDirectory = new File(arguments[0]);
        if (!outputDirectory.exists() && !outputDirectory.mkdirs()) {
            throw new IOException("Could not create output directory: " + outputDirectory);
        }

        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException("Decompiler could not open the program");
        }

        try {
            for (int index = 1; index < arguments.length; ++index) {
                Address address = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(arguments[index]);
                Function function = currentProgram.getFunctionManager()
                    .getFunctionAt(address);
                if (function == null) {
                    printerr("No function starts at " + address);
                    continue;
                }
                DecompileResults results = decompiler.decompileFunction(
                    function, 120, monitor);
                if (!results.decompileCompleted() ||
                    results.getDecompiledFunction() == null) {
                    printerr("Failed to decompile " + function.getName(true) +
                             ": " + results.getErrorMessage());
                    continue;
                }
                File output = new File(
                    outputDirectory,
                    address + "_" + safeFileName(function.getName(true)) + ".cpp");
                try (BufferedWriter writer =
                         new BufferedWriter(new FileWriter(output))) {
                    writer.write("// Binary address: 0x" + address + "\n");
                    writer.write("// Preserved symbol: " +
                                 function.getName(true) + "\n\n");
                    writer.write(results.getDecompiledFunction().getC());
                    writer.write("\n");
                }
            }
        } finally {
            decompiler.dispose();
        }
    }
}
