// Export a bounded set of known entry points without processing resident code.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.Function;
import java.nio.file.*;

public class ExportSelected extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length<2)throw new IllegalArgumentException("Output directory then entry points");
        Path output=Path.of(args[0]);Files.createDirectories(output);
        DecompInterface decompiler=new DecompInterface();decompiler.openProgram(currentProgram);
        try {
            for(int i=1;i<args.length;i++) {
                Function function=getFunctionAt(toAddr(Long.parseUnsignedLong(args[i],16)));
                if(function==null){println("Missing function: "+args[i]);continue;}
                println("Exporting "+args[i]+" / "+function.getBody().getNumAddresses()+" bytes");
                DecompileResults result=decompiler.decompileFunction(function,45,monitor);
                if(result.decompileCompleted()) {
                    Files.writeString(output.resolve(args[i]+".c"),result.getDecompiledFunction().getC());
                    Files.deleteIfExists(output.resolve(args[i]+"-failure.txt"));
                } else {
                    Files.deleteIfExists(output.resolve(args[i]+".c"));
                    Files.writeString(output.resolve(args[i]+"-failure.txt"),result.getErrorMessage());
                    println(args[i]+": "+result.getErrorMessage());
                }
            }
        } finally {decompiler.dispose();}
    }
}
