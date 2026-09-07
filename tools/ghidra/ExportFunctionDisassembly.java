// Export instruction listings for functions whose qualified names match fragments.
// @category OpenAndroidUSM

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class ExportFunctionDisassembly extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] arguments = getScriptArgs();
        if (arguments.length < 2) {
            throw new IllegalArgumentException(
                "Expected output file followed by one or more name fragments");
        }

        List<String> fragments = new ArrayList<>();
        for (int index = 1; index < arguments.length; ++index) {
            fragments.add(arguments[index]);
        }

        try (BufferedWriter writer = new BufferedWriter(new FileWriter(new File(arguments[0])))) {
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                String name = function.getName(true);
                if (fragments.stream().noneMatch(name::contains)) {
                    continue;
                }
                writer.write(name + " @ " + function.getEntryPoint() + "\n");
                for (Instruction instruction : currentProgram.getListing().getInstructions(function.getBody(), true)) {
                    writer.write(String.format(
                        "  %s  %-12s %s\n",
                        instruction.getAddress(),
                        instruction.getMnemonicString(),
                        instruction.toString().substring(instruction.getMnemonicString().length()).trim()));
                }
                writer.write("\n");
            }
        }
    }
}
