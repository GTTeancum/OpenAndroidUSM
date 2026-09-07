// Export bytes at explicitly requested addresses for reviewed reconstruction work.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;

public class ExportAddressBytes extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 3) {
            throw new IllegalArgumentException(
                "Expected output file, byte count, and one or more addresses");
        }

        File output = new File(arguments[0]);
        File parent = output.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs()) {
            throw new IOException("Could not create output directory: " + parent);
        }
        int byteCount = Integer.decode(arguments[1]);

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(output))) {
            for (int argumentIndex = 2; argumentIndex < arguments.length;
                 ++argumentIndex) {
                Address address = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(arguments[argumentIndex]);
                byte[] bytes = new byte[byteCount];
                int read = currentProgram.getMemory().getBytes(address, bytes);

                writer.write("; Data address: 0x" + address + "\n");
                for (int offset = 0; offset < read; offset += 16) {
                    writer.write(address.add(offset) + "  ");
                    for (int index = offset;
                         index < Math.min(offset + 16, read); ++index) {
                        writer.write(String.format("%02x ", bytes[index] & 0xff));
                    }
                    writer.write("\n");
                }
                writer.write("\n");
            }
        }
    }
}
