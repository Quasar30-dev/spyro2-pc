// Temporary case functions for readable analysis of large overlay switches.
// Run with -readOnly: these function boundaries are exploratory, not a repair.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
import java.util.*;
public class ExportMobyCases extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();Path output=Path.of(args[0]);Files.createDirectories(output);
        for(int i=1;i+1<args.length;i+=2) {
            Address start=toAddr(Long.parseUnsignedLong(args[i],16)),end=toAddr(Long.parseUnsignedLong(args[i+1],16));
            AddressSet body=new AddressSet(start,end.subtract(1));
            List<Address> remove=new ArrayList<>();
            for(Function f:currentProgram.getFunctionManager().getFunctions(true))
                if(f.getBody().intersects(body))remove.add(f.getEntryPoint());
            for(Address a:remove)currentProgram.getFunctionManager().removeFunction(a);
            Function f=createFunction(start,"moby_case_"+args[i]);f.setBody(body);
            DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
            try {DecompileResults result=d.decompileFunction(f,45,monitor);
                Files.writeString(output.resolve(args[i]+".c"),result.decompileCompleted()?result.getDecompiledFunction().getC():result.getErrorMessage());
            }finally{d.dispose();}
        }
    }
}
