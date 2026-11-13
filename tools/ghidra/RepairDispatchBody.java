// Keep a large level switch as one function; speculative MIPS entry detection
// otherwise splits shared case/epilogue blocks into spurious functions.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.Function;
import java.util.*;

public class RepairDispatchBody extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        Address start=toAddr(Long.parseUnsignedLong(args[0],16));
        Address end=toAddr(Long.parseUnsignedLong(args[1],16));
        ArrayList<Address> nested=new ArrayList<>();
        for(Function f:currentProgram.getFunctionManager().getFunctions(true))
            if(f.getEntryPoint().compareTo(start)>0 && f.getEntryPoint().compareTo(end)<0)nested.add(f.getEntryPoint());
        for(Address address:nested)currentProgram.getFunctionManager().removeFunction(address);
        Function function=getFunctionAt(start);
        if(function==null)function=createFunction(start,"level_object_dispatch");
        function.setBody(new AddressSet(start,end.subtract(1)));
        println("Repaired dispatch body; removed "+nested.size()+" speculative interior entries");
    }
}
