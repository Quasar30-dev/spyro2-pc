// Read-only evidence export; does not delete files or save the project.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import java.nio.file.*;
public class ExportShadowJarEvidence extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();Path out=Path.of(args[0]);Files.createDirectories(out);
        DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
        try {for(int i=1;i<args.length;i++) {
            var a=toAddr(Long.parseUnsignedLong(args[i],16));var f=getFunctionAt(a);
            if(f==null){println("Missing function "+a);continue;}
            var r=d.decompileFunction(f,45,monitor);
            Files.writeString(out.resolve(args[i]+".c"),r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());
            println("Exported "+a+" "+f.getName());
        }} finally {d.dispose();}
    }
}
