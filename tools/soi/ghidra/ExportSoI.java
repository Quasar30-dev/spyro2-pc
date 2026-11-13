// Export function inventory, call edges and decompiled C for the SoI ROM into one directory.
// Args: <outdir> [all|list=addr,addr,...]
// @category SoI
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.nio.file.*;
import java.io.*;
import java.util.*;

public class ExportSoI extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        Path root = Path.of(args[0]);
        Files.createDirectories(root);
        Set<Long> only = null;
        boolean all = args.length > 1 && args[1].equals("all");
        if (args.length > 1 && args[1].startsWith("list=")) {
            only = new HashSet<>();
            for (String s : args[1].substring(5).split(",")) only.add(Long.parseLong(s.replace("0x",""), 16));
        }
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        int n = 0, ok = 0, bad = 0;
        String suffix = only != null ? "_sel" : "";
        try (PrintWriter inv = new PrintWriter(Files.newBufferedWriter(root.resolve("functions" + suffix + ".tsv")));
             PrintWriter calls = new PrintWriter(Files.newBufferedWriter(root.resolve("calls" + suffix + ".tsv")));
             PrintWriter c = new PrintWriter(Files.newBufferedWriter(root.resolve("decompiled" + suffix + ".c")))) {
            inv.println("address\tname\tbody_bytes\tthumb");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                monitor.checkCancelled();
                n++;
                long ea = f.getEntryPoint().getOffset();
                var tm = currentProgram.getProgramContext().getValue(currentProgram.getRegister("TMode"), f.getEntryPoint(), false);
                inv.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses() + "\t" + (tm == null ? "?" : tm));
                for (Function g : f.getCalledFunctions(monitor)) calls.println(f.getEntryPoint() + "\t" + g.getEntryPoint());
                if (!(all || (only != null && only.contains(ea)))) continue;
                DecompileResults r = dec.decompileFunction(f, 30, monitor);
                if (r.decompileCompleted()) { c.println("// ==== " + f.getEntryPoint() + " " + f.getName()); c.println(r.getDecompiledFunction().getC()); ok++; }
                else { c.println("// ==== " + f.getEntryPoint() + " FAILED " + r.getErrorMessage()); bad++; }
            }
        } finally { dec.dispose(); }
        println("ExportSoI: functions=" + n + " decompiled=" + ok + " failed=" + bad);
    }
}
