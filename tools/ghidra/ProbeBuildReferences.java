// Trace prerelease data references without changing an existing project.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.nio.file.*;
import java.util.*;
public class ProbeBuildReferences extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();Path out=Path.of(args[0]);Files.createDirectories(out);
        StringBuilder report=new StringBuilder();Set<Function> functions=new LinkedHashSet<>();
        for(int i=1;i<args.length;i++) {
            var target=toAddr(Long.parseUnsignedLong(args[i],16));
            for(Reference r:getReferencesTo(target)) {
                Function f=getFunctionContaining(r.getFromAddress());
                report.append(target+"\t"+r.getFromAddress()+"\t"+(f==null?"no function":f.getEntryPoint())+"\n");
                if(f!=null)functions.add(f);
            }
        }
        Files.writeString(out.resolve("references.tsv"),report.toString());
        DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
        try {for(Function f:functions) {
            var r=d.decompileFunction(f,45,monitor);
            if(r.decompileCompleted())Files.writeString(out.resolve(f.getEntryPoint()+".c"),r.getDecompiledFunction().getC());
        }} finally {d.dispose();}
    }
}
