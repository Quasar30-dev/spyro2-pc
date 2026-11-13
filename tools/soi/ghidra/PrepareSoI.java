// Pre-analysis setup for Spyro: Season of Ice (GBA, ASYE) loaded as a raw binary at 0x08000000.
// Adds the GBA memory map and seeds THUMB functions from odd ROM pointers whose target is a
// push {..,lr} prologue. Research tooling for the Season of Ice port prep; read-only on the ROM.
// @category SoI
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.*;
import java.math.BigInteger;

public class PrepareSoI extends GhidraScript {
    void block(String name, long start, long size, boolean write, boolean exec) throws Exception {
        Memory mem = currentProgram.getMemory();
        Address a = toAddr(start);
        if (mem.getBlock(a) != null) return;
        MemoryBlock b = mem.createUninitializedBlock(name, a, size, false);
        b.setRead(true); b.setWrite(write); b.setExecute(exec);
    }
    @Override public void run() throws Exception {
        block("EWRAM", 0x02000000L, 0x40000, true, true);
        block("IWRAM", 0x03000000L, 0x8000, true, true);
        block("IO", 0x04000000L, 0x400, true, false);
        block("PAL", 0x05000000L, 0x400, true, false);
        block("VRAM", 0x06000000L, 0x18000, true, false);
        block("OAM", 0x07000000L, 0x400, true, false);
        Memory mem = currentProgram.getMemory();
        MemoryBlock rom = mem.getBlock(toAddr(0x08000000L));
        rom.setWrite(false); rom.setExecute(true);
        Register tmode = currentProgram.getRegister("TMode");
        // ARM entry.
        currentProgram.getSymbolTable().addExternalEntryPoint(toAddr(0x08000000L));
        DisassembleCommand arm = new DisassembleCommand(toAddr(0x08000000L), null, true);
        arm.applyTo(currentProgram, monitor);
        int seeded = 0;
        long size = rom.getSize();
        for (long off = 0; off + 4 <= size; off += 4) {
            int w = mem.getInt(toAddr(0x08000000L + off));
            long u = w & 0xffffffffL;
            if (u < 0x08000001L || u >= 0x08000000L + size || (u & 1) == 0) continue;
            long t = u - 1;
            int h = mem.getShort(toAddr(t)) & 0xffff;
            if ((h & 0xff00) != 0xb500) continue;
            Address ta = toAddr(t);
            if (getInstructionAt(ta) != null) continue;
            currentProgram.getProgramContext().setValue(tmode, ta, ta.add(1), BigInteger.ONE);
            DisassembleCommand cmd = new DisassembleCommand(ta, null, true);
            cmd.applyTo(currentProgram, monitor);
            createFunction(ta, null);
            seeded++;
        }
        println("PrepareSoI: seeded " + seeded + " THUMB functions from ROM pointers");
    }
}
