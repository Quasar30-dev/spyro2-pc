// Read-only inventory. Function discovery and names are not semantic completion.
// @category Spyro2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.nio.file.*;
import java.io.*;

public class AuditMapping extends GhidraScript {
    @Override public void run() throws Exception {
        Path out=Path.of(getScriptArgs()[0]);Files.createDirectories(out);
        try(PrintWriter funcs=new PrintWriter(Files.newBufferedWriter(out.resolve("functions.tsv")));
            PrintWriter blocks=new PrintWriter(Files.newBufferedWriter(out.resolve("blocks.tsv")))) {
            funcs.println("address\tname\tblock\tbody_bytes\tsymbol_source\tname_category");
            blocks.println("block\tstart\tend\tbytes\texecutable\tdefined_instruction_bytes\tfunctions\tnamed\tcandidate\tautomatic\tfunction_body_bytes");
            for(MemoryBlock block:currentProgram.getMemory().getBlocks()) {
                if(!block.isInitialized())continue;
                AddressSet range=new AddressSet(block.getStart(),block.getEnd());
                long instructions=0,body=0;int count=0,named=0,candidate=0,automatic=0;
                for(Instruction instruction:currentProgram.getListing().getInstructions(range,true))instructions+=instruction.getLength();
                for(Function f:currentProgram.getFunctionManager().getFunctions(true)) {
                    if(!block.contains(f.getEntryPoint()))continue;
                    count++;String name=f.getName(),category;
                    if(name.startsWith("candidate_") || name.startsWith("level_callback_") || name.startsWith("overlay_dispatch_")){candidate++;category="candidate_or_seed";}
                    else if(name.startsWith("FUN_") || name.startsWith("thunk_FUN_")){automatic++;category="automatic";}
                    else {named++;category="named_not_proof_of_matching";}
                    long size=f.getBody().intersect(range).getNumAddresses();body+=size;
                    funcs.println(f.getEntryPoint()+"\t"+name+"\t"+block.getName()+"\t"+size+"\t"+f.getSymbol().getSource()+"\t"+category);
                }
                blocks.println(block.getName()+"\t"+block.getStart()+"\t"+block.getEnd()+"\t"+block.getSize()+"\t"+block.isExecute()+"\t"+instructions+"\t"+count+"\t"+named+"\t"+candidate+"\t"+automatic+"\t"+body);
            }
        }
        Files.writeString(out.resolve("program.txt"),currentProgram.getName()+"\n"+currentProgram.getLanguageID()+"\n"+currentProgram.getExecutableMD5()+"\n");
        println("Read-only audit written to "+out);
    }
}
