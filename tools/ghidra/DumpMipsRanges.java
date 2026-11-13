// Focused instruction evidence for packed formats, including GTE-heavy routines.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;
import java.nio.file.*;
import java.io.*;

public class DumpMipsRanges extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length<3 || args.length%2!=1)throw new IllegalArgumentException("Output path then start/end hex pairs");
        Path path=Path.of(args[0]);Files.createDirectories(path.toAbsolutePath().getParent());
        try(PrintWriter out=new PrintWriter(Files.newBufferedWriter(path))) {
            for(int i=1;i<args.length;i+=2) {
                long start=Long.parseUnsignedLong(args[i].replace("0x",""),16);
                long end=Long.parseUnsignedLong(args[i+1].replace("0x",""),16);
                for(long address=start;address<end;address+=4) {
                    monitor.checkCancelled();
                    Instruction instruction=getInstructionAt(toAddr(address));
                    out.printf("%08x  %08x  %s%n",address,getInt(toAddr(address)),instruction==null?"(undisassembled)":instruction.toString());
                }
            }
        }
    }
}
