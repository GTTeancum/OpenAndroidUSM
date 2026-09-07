// Export all stored occurrences and references for selected IEEE-754 values.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;

public class ExportFloatReferences extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output file followed by one or more float values");
        }

        Memory memory = currentProgram.getMemory();
        try (BufferedWriter writer =
                 new BufferedWriter(new FileWriter(new File(arguments[0])))) {
            for (int argument = 1; argument < arguments.length; ++argument) {
                float value = Float.parseFloat(arguments[argument]);
                int bits = Float.floatToRawIntBits(value);
                byte[] pattern = new byte[] {
                    (byte)(bits & 0xff),
                    (byte)((bits >>> 8) & 0xff),
                    (byte)((bits >>> 16) & 0xff),
                    (byte)((bits >>> 24) & 0xff)
                };
                writer.write(value + " (0x" + Integer.toHexString(bits) + ")\n");
                for (MemoryBlock block : memory.getBlocks()) {
                    if (!block.isInitialized()) {
                        continue;
                    }
                    Address start = block.getStart();
                    Address end = block.getEnd();
                    Address match = memory.findBytes(
                        start, end, pattern, null, true, monitor);
                    while (match != null && !monitor.isCancelled()) {
                        writer.write("  " + match + " " + block.getName() + "\n");
                        ReferenceIterator references = currentProgram
                            .getReferenceManager().getReferencesTo(match);
                        while (references.hasNext()) {
                            Reference reference = references.next();
                            Function function = currentProgram
                                .getFunctionManager()
                                .getFunctionContaining(reference.getFromAddress());
                            writer.write("    " + reference.getFromAddress() + " " +
                                reference.getReferenceType() + " " +
                                (function == null ? "<data>" :
                                 function.getName(true)) + "\n");
                        }
                        if (match.equals(end)) {
                            break;
                        }
                        match = memory.findBytes(
                            match.next(), end, pattern, null, true, monitor);
                    }
                }
            }
        }
    }
}
