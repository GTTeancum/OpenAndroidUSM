// Export reviewed seed functions as instruction listings for manual reconstruction.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class ExportSelectedDisassembly extends GhidraScript {
    private static String safeFileName(String value) {
        String safe = value.replaceAll("[^A-Za-z0-9_.-]", "_");
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

        int count = 0;
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            Function function = functions.next();
            String qualifiedName = function.getName(true);
            boolean selected = fragments.stream().anyMatch(qualifiedName::contains);
            if (!selected) {
                continue;
            }

            String fileName = function.getEntryPoint() + "_" +
                              safeFileName(qualifiedName) + ".txt";
            File output = new File(outputDirectory, fileName);
            try (BufferedWriter writer = new BufferedWriter(new FileWriter(output))) {
                writer.write("; Binary address: 0x" + function.getEntryPoint() + "\n");
                writer.write("; Preserved symbol: " + qualifiedName + "\n\n");
                InstructionIterator instructions =
                    currentProgram.getListing().getInstructions(function.getBody(), true);
                while (instructions.hasNext()) {
                    Instruction instruction = instructions.next();
                    writer.write(instruction.getAddress() + "  " + instruction + "\n");
                }
                writer.write("\n; 32 bytes following the function (literal pool):\n");
                Address trailing = function.getBody().getMaxAddress().next();
                byte[] bytes = new byte[32];
                int read = currentProgram.getMemory().getBytes(trailing, bytes);
                for (int offset = 0; offset < read; offset += 16) {
                    writer.write(trailing.add(offset) + "  ");
                    for (int index = offset; index < Math.min(offset + 16, read); index++) {
                        writer.write(String.format("%02x ", bytes[index] & 0xff));
                    }
                    writer.write("\n");
                }
            }
            count++;
        }
        println("Exported " + count + " selected disassemblies to " +
                outputDirectory.getAbsolutePath());
    }
}
