// Overlay imports remain separate programs: different levels reuse RAM.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.io.*;

public class PrepareOverlay extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("Expected main payload path [explicit entry points]");
        MemoryBlock overlay = currentProgram.getMemory().getBlock(toAddr(0x8006d264L));
        if (overlay == null) throw new IllegalStateException("Missing overlay");
        overlay.setName("overlay"); overlay.setRead(true); overlay.setWrite(true); overlay.setExecute(true);
        byte[] main = Files.readAllBytes(Path.of(args[0]));
        MemoryBlock executable = currentProgram.getMemory().createInitializedBlock("main_executable",
            toAddr(0x80010000L), new ByteArrayInputStream(main), main.length, monitor, false);
        executable.setRead(true); executable.setWrite(true); executable.setExecute(true);
        MemoryBlock globals = currentProgram.getMemory().createUninitializedBlock("main_globals", toAddr(0x80067000L), 0x6264, false);
        globals.setRead(true); globals.setWrite(true); globals.setExecute(false);
        long end = overlay.getEnd().getOffset()+1;
        MemoryBlock remaining = currentProgram.getMemory().createUninitializedBlock("remaining_ram", toAddr(end), 0x80200000L-end, false);
        remaining.setRead(true); remaining.setWrite(true); remaining.setExecute(false);
        if (args.length > 1) {
            // Level callbacks are selected by the loader overlay. Its own
            // resident call targets must not be treated as level entry points.
            for (int i=1;i<args.length;i++) {
                Address address=toAddr(Long.parseUnsignedLong(args[i],16));
                if(!overlay.contains(address))throw new IllegalArgumentException("Entry is outside overlay");
                disassemble(address);
                if(getFunctionAt(address)==null)createFunction(address,"level_callback_"+args[i]);
                currentProgram.getSymbolTable().addExternalEntryPoint(address);
            }
            return;
        }
        // The first word is a build/level identifier, followed by a dispatch
        // table. Seed only in-range, aligned pointers with a MIPS stack prologue.
        int seeds = 0;
        for (long offset=4; offset<0x600; offset+=4) {
            long target = Integer.toUnsignedLong(getInt(toAddr(0x8006d264L+offset)));
            if (target < 0x8006d264L+0x100 || target+4>end || (target&3)!=0) continue;
            int word = getInt(toAddr(target));
            if ((word & 0xffff0000) != 0x27bd0000) continue;
            Address address=toAddr(target);
            disassemble(address);
            if (getFunctionAt(address)==null) createFunction(address,"overlay_dispatch_"+Long.toHexString(target));
            seeds++;
        }
        // Main code references supply additional entry points into overlays.
        disassemble(toAddr(0x8005478cL));
        createFunction(toAddr(0x8005478cL),"psx_entry");
        currentProgram.getSymbolTable().addExternalEntryPoint(toAddr(0x8005478cL));
        println("Overlay mapped separately at 8006d264; dispatch seeds: " + seeds);
    }
}
