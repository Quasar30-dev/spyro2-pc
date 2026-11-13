// Prepare a prerelease executable in an isolated Ghidra project.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class PrepareBuild extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();long entry=Long.parseUnsignedLong(args[0],16);
        MemoryBlock payload=currentProgram.getMemory().getBlock(toAddr(entry));
        if(payload==null)throw new IllegalArgumentException("Entry is outside loaded executable");
        payload.setName("main_executable");payload.setRead(true);payload.setWrite(true);payload.setExecute(true);
        long end=payload.getEnd().getOffset()+1;
        MemoryBlock ram=currentProgram.getMemory().createUninitializedBlock("uninitialized_ram",toAddr(end),0x80200000L-end,false);
        ram.setRead(true);ram.setWrite(true);ram.setExecute(false);
        disassemble(toAddr(entry));createFunction(toAddr(entry),"psx_entry");
        currentProgram.getSymbolTable().addExternalEntryPoint(toAddr(entry));
        if(args.length>1)for(String line:Files.readAllLines(Path.of(args[1]))) {
            if(line.isBlank() || line.startsWith("#"))continue;
            String[] fields=line.split("\t");Address address=toAddr(Long.parseUnsignedLong(fields[0],16));
            createLabel(address,fields[1],true,SourceType.USER_DEFINED);
            setPlateComment(address,fields.length>2?fields[2]:"Prerelease data location; see build comparison evidence.");
        }
    }
}
