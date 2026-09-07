// Export instructions whose operands contain one of the requested scalar values.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.HashSet;
import java.util.Set;

public class ExportInstructionScalarMatches extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output file and one or more scalar values");
        }
        Set<Long> values = new HashSet<>();
        for (int index = 1; index < arguments.length; ++index) {
            values.add(Long.decode(arguments[index]));
        }

        try (BufferedWriter writer =
                 new BufferedWriter(new FileWriter(new File(arguments[0])))) {
            InstructionIterator instructions =
                currentProgram.getListing().getInstructions(true);
            while (instructions.hasNext() && !monitor.isCancelled()) {
                Instruction instruction = instructions.next();
                boolean matches = false;
                for (int operand = 0;
                     operand < instruction.getNumOperands() && !matches;
                     ++operand) {
                    for (Object object : instruction.getOpObjects(operand)) {
                        if (object instanceof Scalar scalar &&
                            values.contains(scalar.getUnsignedValue())) {
                            matches = true;
                            break;
                        }
                    }
                }
                if (!matches) {
                    continue;
                }
                Function function = currentProgram.getFunctionManager()
                    .getFunctionContaining(instruction.getAddress());
                writer.write(instruction.getAddress() + "  " + instruction +
                    "  " + (function == null ? "<data>" :
                              function.getName(true)) + "\n");
            }
        }
    }
}
