// Read-only exploration: cut the shared dispatch loop at its common epilogue.
// Without this terminal stub, the decompiler follows it back into every Moby.
// Raw overlay bytes and the existing database are never saved by this script.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
import java.util.*;
public class ExportRiptoBoss extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();Path out=Path.of(args[0]);Files.createDirectories(out);
        // Optional common epilogue permits another retail overlay's case.
        int first=1;long epilogue=0x80088164L;
        if(args.length>2 && args[1].equals("--stop")){epilogue=Long.parseUnsignedLong(args[2],16);first=3;}
        Address stop=toAddr(epilogue);
        clearListing(stop,stop.add(7));setInt(stop,0x03e00008);setInt(stop.add(4),0);disassemble(stop);
        for(int i=first;i+1<args.length;i+=2) {
            Address start=toAddr(Long.parseUnsignedLong(args[i],16)),end=toAddr(Long.parseUnsignedLong(args[i+1],16));
            AddressSet body=new AddressSet(start,end.subtract(1));body.add(stop,stop.add(7));
            List<Address> remove=new ArrayList<>();
            for(Function f:currentProgram.getFunctionManager().getFunctions(true))if(f.getBody().intersects(body))remove.add(f.getEntryPoint());
            for(Address a:remove)currentProgram.getFunctionManager().removeFunction(a);
            disassemble(start);Function f=createFunction(start,"ripto_case_"+args[i]);f.setBody(body);
            DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
            try {println("Exporting "+args[i]);DecompileResults r=d.decompileFunction(f,60,monitor);
                Files.writeString(out.resolve(args[i]+".c"),r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());
            }finally{d.dispose();}
        }
    }
}
