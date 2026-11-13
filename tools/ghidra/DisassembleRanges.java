// Explicitly decode switch-case bodies missed without full auto-analysis.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.AddressSet;
public class DisassembleRanges extends GhidraScript {
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        for(int i=0;i+1<args.length;i+=2) {
            var start=toAddr(Long.parseUnsignedLong(args[i],16));
            var end=toAddr(Long.parseUnsignedLong(args[i+1],16));
            var bounds=new AddressSet(start,end.subtract(1));
            for(var at=start;at.compareTo(end)<0;at=at.add(4)) {
                monitor.checkCancelled();
                if(getInstructionAt(at)==null)new DisassembleCommand(at,bounds,true).applyTo(currentProgram,monitor);
            }
        }
    }
}
