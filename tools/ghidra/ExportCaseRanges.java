// Decompile moby dispatch case bodies as temporary functions, without the
// whole-overlay auto-analysis (which times out on the giant dispatchers).
// Arguments: <output dir> (<start hex> <end hex> <label>)...
// Ranges come from tools/moby_dispatch.py. The boundaries are exploratory:
// a case body is not asserted to be an original function.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
import java.util.*;
public class ExportCaseRanges extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();Path output=Path.of(args[0]);Files.createDirectories(output);
        DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
        try {
            for(int i=1;i+2<args.length;i+=3) {
                Address start=toAddr(Long.parseUnsignedLong(args[i],16)),end=toAddr(Long.parseUnsignedLong(args[i+1],16));
                String label=args[i+2];
                AddressSet body=new AddressSet(start,end.subtract(1));
                disassemble(start);
                // Cover the whole case body even where flow analysis stops early.
                for(Address a=start;a.compareTo(end)<0;a=a.add(4))if(getInstructionAt(a)==null)disassemble(a);
                List<Address> remove=new ArrayList<>();
                for(Function f:currentProgram.getFunctionManager().getFunctions(true))if(f.getBody().intersects(body))remove.add(f.getEntryPoint());
                for(Address a:remove)currentProgram.getFunctionManager().removeFunction(a);
                Function f=createFunction(start,"case_"+label);
                if(f==null){Files.writeString(output.resolve(label+".c"),"// could not create a function at "+start);continue;}
                f.setBody(body);
                DecompileResults result=d.decompileFunction(f,900,monitor);
                Files.writeString(output.resolve(label+".c"),"// "+label+" case "+start+".."+end+"\n"+
                    (result.decompileCompleted()?result.getDecompiledFunction().getC():result.getErrorMessage()));
                println("exported "+label);
            }
        } finally {d.dispose();}
    }
}
