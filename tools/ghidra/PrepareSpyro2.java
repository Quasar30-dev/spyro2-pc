// Prepare a raw, header-stripped SCUS_944.25 at 0x80010000.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class PrepareSpyro2 extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) throw new IllegalArgumentException("Expected symbols TSV path");
        MemoryBlock payload = currentProgram.getMemory().getBlock(toAddr(0x80010000L));
        if (payload == null) throw new IllegalStateException("Missing executable at 80010000");
        payload.setName("main_executable");
        payload.setRead(true); payload.setWrite(true); payload.setExecute(true);
        // BSS length is zero in the PS-X EXE header. Map the remaining PS1 RAM as
        // uninitialized storage, not initialized code, for pointer/xref analysis.
        long end = payload.getEnd().getOffset() + 1;
        if (currentProgram.getMemory().getBlock(toAddr(end)) == null) {
            MemoryBlock ram = currentProgram.getMemory().createUninitializedBlock(
                "uninitialized_ram", toAddr(end), 0x80200000L-end, false);
            ram.setRead(true); ram.setWrite(true); ram.setExecute(false);
        }
        currentProgram.getProgramContext().setValue(currentProgram.getRegister("sp"),
            toAddr(0x8005478cL), toAddr(0x8005478cL), java.math.BigInteger.valueOf(0x801ffff0L));
        for (String line : Files.readAllLines(Path.of(args[0]))) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] fields = line.split("\t");
            Address address = toAddr(Long.parseUnsignedLong(fields[0], 16));
            if (!currentProgram.getMemory().contains(address)) continue;
            createLabel(address, fields[1], true, SourceType.USER_DEFINED);
            setPlateComment(address, "Symbol provenance: " + fields[3] +
                ". Candidate names are research leads, not verified matching decompilation.");
            if (fields[2].equals("function")) {
                disassemble(address);
                Function function = getFunctionAt(address);
                if (function == null) function = createFunction(address, fields[1]);
                if (function != null) function.setName(fields[1], SourceType.USER_DEFINED);
            }
        }
        currentProgram.getSymbolTable().addExternalEntryPoint(toAddr(0x8005478cL));
        println("Prepared SCUS_944.25 RAM map, entry point and provenance-marked research symbols.");
    }
}
