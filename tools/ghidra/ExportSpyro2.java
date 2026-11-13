// Export reviewable function inventory, call edges and selected decompilation.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.io.*;

public class ExportSpyro2 extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("Expected output directory");
        Path root = Path.of(args[0]);
        Files.createDirectories(root.resolve("decompiled"));
        // Export directory is dedicated to this script: remove stale generated
        // C files so a revised symbol map cannot leave misleading old names.
        try (var old = Files.list(root.resolve("decompiled"))) {
            for (Path path : old.filter(p -> p.getFileName().toString().endsWith(".c")).toList())
                Files.delete(path);
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        int functions = 0, completed = 0, failed = 0, unloaded = 0;
        boolean all = args.length > 1 && args[1].equals("all");
        try (PrintWriter inventory = new PrintWriter(Files.newBufferedWriter(root.resolve("functions.tsv")));
             PrintWriter calls = new PrintWriter(Files.newBufferedWriter(root.resolve("calls.tsv")));
             PrintWriter failures = new PrintWriter(Files.newBufferedWriter(root.resolve("decompile_failures.tsv")))) {
            inventory.println("address\tname\tbody_bytes\tdecompiled");
            calls.println("caller\tcallee\treference");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                monitor.checkCancelled();
                boolean exported = false;
                functions++;
                boolean initialized = currentProgram.getMemory().getBlock(f.getEntryPoint()) != null &&
                    currentProgram.getMemory().getBlock(f.getEntryPoint()).isInitialized();
                if (!initialized) unloaded++;
                if (initialized && (all || f.getName().startsWith("candidate_") || f.getName().equals("psx_entry"))) {
                    DecompileResults result = decompiler.decompileFunction(f, 45, monitor);
                    if (result.decompileCompleted()) {
                        String code = "/* Ghidra output: " + currentProgram.getName() + " @ " + f.getEntryPoint() +
                            ". Research only; types/control flow may be incorrect. */\n" + result.getDecompiledFunction().getC();
                        Files.writeString(root.resolve("decompiled").resolve(f.getEntryPoint()+"_"+f.getName()+".c"), code, StandardCharsets.UTF_8);
                        completed++; exported = true;
                    } else { failures.println(f.getEntryPoint()+"\t"+result.getErrorMessage()); failed++; }
                }
                inventory.println(f.getEntryPoint()+"\t"+f.getName()+"\t"+f.getBody().getNumAddresses()+"\t"+exported);
                for (Function called : f.getCalledFunctions(monitor))
                    calls.println(f.getEntryPoint()+"\t"+called.getEntryPoint()+"\tcall");
            }
        } finally { decompiler.dispose(); }
        String summary = "{\n  \"program\": \""+currentProgram.getName()+"\",\n  \"language\": \""+
            currentProgram.getLanguageID()+"\",\n  \"functions\": "+functions+",\n  \"decompiled\": "+completed+
            ",\n  \"failed\": "+failed+",\n  \"unloaded_function_targets\": "+unloaded+"\n}\n";
        Files.writeString(root.resolve("summary.json"), summary);
        println(summary);
    }
}
