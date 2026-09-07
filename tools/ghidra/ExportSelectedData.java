// Export bytes at named data symbols for reviewed reconstruction work.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

public class ExportSelectedData extends GhidraScript {
    private static String safeFileName(String value) {
        String safe = value.replaceAll("[^A-Za-z0-9_.-]", "_");
        return safe.length() <= 96 ? safe : safe.substring(0, 96);
    }

    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 3) {
            throw new IllegalArgumentException(
                "Expected output directory, byte count, and name fragments");
        }
        File outputDirectory = new File(arguments[0]);
        if (!outputDirectory.exists() && !outputDirectory.mkdirs()) {
            throw new IOException("Could not create output directory: " + outputDirectory);
        }
        int byteCount = Integer.parseInt(arguments[1]);
        List<String> fragments = new ArrayList<>();
        for (int index = 2; index < arguments.length; ++index) {
            fragments.add(arguments[index]);
        }

        int count = 0;
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (symbols.hasNext() && !monitor.isCancelled()) {
            Symbol symbol = symbols.next();
            String qualifiedName = symbol.getName(true);
            if (fragments.stream().noneMatch(qualifiedName::contains)) {
                continue;
            }
            Address address = symbol.getAddress();
            byte[] bytes = new byte[byteCount];
            int read;
            try {
                read = currentProgram.getMemory().getBytes(address, bytes);
            } catch (Exception exception) {
                println("Skipping unreadable symbol " + qualifiedName +
                        " at " + address);
                continue;
            }
            File output = new File(outputDirectory,
                address + "_" + safeFileName(qualifiedName) + ".txt");
            try (BufferedWriter writer = new BufferedWriter(new FileWriter(output))) {
                writer.write("; Data address: 0x" + address + "\n");
                writer.write("; Preserved symbol: " + qualifiedName + "\n\n");
                for (int offset = 0; offset < read; offset += 16) {
                    writer.write(address.add(offset) + "  ");
                    for (int index = offset; index < Math.min(offset + 16, read); ++index) {
                        writer.write(String.format("%02x ", bytes[index] & 0xff));
                    }
                    writer.write("\n");
                }
            }
            ++count;
        }
        println("Exported " + count + " selected data symbols to " +
                outputDirectory.getAbsolutePath());
    }
}
