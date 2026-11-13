"""Phase 3 MIPS actor adapters, executed by both original and native bridges.

This is authored code, not a new resident executable. The original class cases
and content04 C9 extension remain byte-identical except the documented entry
trampoline. No level-ID constant occurs in this adapter.
"""
import struct

BASE = 0x8006D264
REG = {n:i for i,n in enumerate('zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra'.split())}


class Asm:
    def __init__(self, address):
        self.base=address;self.words=[];self.labels={};self.fixups=[]
    @property
    def pc(self):return self.base+4*len(self.words)
    def emit(self,w):self.words.append(w&0xffffffff)
    def label(self,n):
        assert n not in self.labels;n0=self.pc;self.labels[n]=n0;return n0
    def nop(self):self.emit(0)
    def imm(self,op,rt,rs,v):self.emit(op<<26|REG[rs]<<21|REG[rt]<<16|(v&65535))
    def addiu(self,rt,rs,v):self.imm(9,rt,rs,v)
    def li(self,rt,v):
        if -32768<=v<=32767:self.addiu(rt,'zero',v)
        else:self.imm(15,rt,'zero',v>>16);self.imm(13,rt,rt,v)
    def r(self,fn,rd,rs,rt='zero'):self.emit(REG[rs]<<21|REG[rt]<<16|REG[rd]<<11|fn)
    def move(self,rd,rs):self.r(0x21,rd,rs)
    def addu(self,rd,rs,rt):self.r(0x21,rd,rs,rt)
    def subu(self,rd,rs,rt):self.r(0x23,rd,rs,rt)
    def slt(self,rd,rs,rt):self.r(0x2a,rd,rs,rt)
    def shift(self,rd,rt,n,arithmetic=False):self.emit(REG[rt]<<16|REG[rd]<<11|n<<6|(3 if arithmetic else 2))
    def load(self,op,rt,at,rs):self.imm(op,rt,rs,at);self.nop()
    def lw(self,rt,at,rs):self.load(35,rt,at,rs)
    def lbu(self,rt,at,rs):self.load(36,rt,at,rs)
    def lh(self,rt,at,rs):self.load(33,rt,at,rs)
    def sw(self,rt,at,rs):self.imm(43,rt,rs,at)
    def sb(self,rt,at,rs):self.imm(40,rt,rs,at)
    def sh(self,rt,at,rs):self.imm(41,rt,rs,at)
    def branch(self,op,rs,rt,target):
        self.fixups.append((len(self.words),'branch',target));self.imm(op,rt,rs,0);self.nop()
    def beq(self,rs,rt,target):self.branch(4,rs,rt,target)
    def bne(self,rs,rt,target):self.branch(5,rs,rt,target)
    def jump(self,target,call=False):
        self.fixups.append((len(self.words),'jal' if call else 'j',target));self.emit(0);self.nop()
    def jal(self,target):self.jump(target,True)
    def jr(self,rs):self.emit(REG[rs]<<21|8);self.nop()
    def finish(self):
        for i,kind,target in self.fixups:
            t=self.labels[target] if isinstance(target,str) else target
            if kind=='branch':
                n=(t-(self.base+4*i)-4)//4;assert -32768<=n<=32767
                self.words[i]=(self.words[i]&0xffff0000)|(n&65535)
            else:self.words[i]=(3 if kind=='jal' else 2)<<26|((t>>2)&0x3ffffff)
        return struct.pack('<'+'I'*len(self.words),*self.words)


def build(original):
    assert len(original)==84020
    assert struct.unpack_from('<II',original,0x80070478-BASE)==(0x240201aa,0x106212e9)
    code=bytearray(original);code.extend(bytes((-len(code))%16));a=Asm(BASE+len(code));entry=a.pc
    a.label('dispatch')
    for cls in range(0x2e0,0x2e8):
        a.li('t0',cls);a.beq('v1','t0','adapt')
    # Replay both overwritten instructions and their original delay slot.
    a.li('v0',0x1aa);a.beq('v1','v0',0x80075024)
    a.words[-1]=(10<<26)|(REG['v1']<<21)|(REG['v0']<<16)|0x1ab
    a.jump(0x80070484)
    a.label('adapt');a.move('a0','s4');a.jal('tick');a.jump(0x8007e1ac)

    a.label('tick')
    a.addiu('sp','sp',-64)
    for i,r in enumerate(['ra','s0','s1','s2','s3','s4','s5']):a.sw(r,60-i*4,'sp')
    a.move('s0','a0');a.lw('s1',0,'s0');a.lw('s2',4,'s1')
    a.li('t0',4);a.beq('s2','t0','attached_fan')
    # Params are private per actor, magic checked by the bounded verifier.
    a.lw('t0',20,'s1');a.li('t1',0x80067018);a.lw('s5',0,'t1')
    a.addu('t0','t0','s5');a.sw('t0',20,'s1')
    a.lw('t0',24,'s1');a.addu('t0','t0','s5');a.sw('t0',24,'s1')
    a.li('t0',32);a.sb('t0',76,'s0');a.sb('zero',77,'s0')
    # The original animation clock has already run. Install a passive body
    # cylinder for physical flame/charge queries, independent of donor attacks.
    a.addiu('t0','s1',64);a.sw('t0',8,'s0')
    # Animated banks without donor collision (rabbit/bells) start outside the
    # original spatial grid. Reproduce 800414cc..80041504 once; updating an
    # unregistered record with 80041538 flag2 deliberately does nothing.
    a.lh('t0',52,'s0');a.slt('t0','t0','zero');a.beq('t0','zero','body_registered')
    a.lw('t0',12,'s0');a.shift('t0','t0',13);a.lw('t1',16,'s0');a.shift('t1','t1',13)
    a.emit(REG['t1']<<16|REG['t1']<<11|5<<6);a.addu('t0','t0','t1');a.sh('t0',52,'s0')
    a.emit(REG['t0']<<16|REG['t0']<<11|2<<6);a.li('t1',0x80067174);a.lw('t1',0,'t1');a.addu('t0','t0','t1')
    a.lw('t1',0,'t0');a.sw('s0',0,'t0');a.sw('t1',4,'s0');a.label('body_registered')
    a.li('t0',5);a.beq('s2','t0','bell')
    a.li('t0',6);a.beq('s2','t0','quest_controller')
    a.li('t0',7);a.beq('s2','t0','quest_portal')
    a.beq('s2','zero','greeter')
    a.li('t0',3);a.beq('s2','t0','critter')
    a.lbu('s4',72,'s0');a.li('t0',6);a.beq('s4','t0','dying')
    # Both ordinary port attacks are supported. GBA's live message-5 death
    # is translated to flame; PS1 charge additionally uses the same death path.
    a.lw('t0',24,'s0');a.li('t1',0x00170000);a.r(0x24,'t0','t0','t1')
    a.sw('zero',24,'s0');a.beq('t0','zero','alive')
    a.li('a1',6);a.jal('state');a.li('t0',1);a.beq('s2','t0','spear_hurt')
    a.li('a1',4);a.jump('start_hurt')
    a.label('spear_hurt');a.li('a1',6)
    a.label('start_hurt');a.jal('clip');a.sw('zero',8,'s0');a.jump('out')

    a.label('alive');a.jal('distance');a.move('s3','v0')
    # Original SoI callbacks hurt on body contact as well as the forward
    # weapon/fan envelope. Vertical gating keeps adjacent terraces separate.
    a.imm(10,'t0','s3',768);a.beq('t0','zero','body_contact_end')
    a.li('t0',0x80069ff8);a.lw('t0',0,'t0');a.lw('t1',20,'s0');a.subu('t0','t0','t1');a.addiu('t0','t0',768);a.imm(11,'t0','t0',1536);a.beq('t0','zero','body_contact_end')
    a.jal('hurt')
    a.label('body_contact_end')
    # Turn toward Spyro using the original atan routine, 8-bit moby heading.
    a.li('t0',0x80069ff0);a.lw('t1',0,'t0');a.lw('t2',12,'s0');a.subu('a0','t1','t2')
    a.lw('t1',4,'t0');a.lw('t2',16,'s0');a.subu('a1','t1','t2');a.move('a2','zero');a.jal(0x8001b4b8);a.sb('v0',70,'s0')
    a.lw('t0',24,'s1');a.li('t1',2);a.beq('s4','t1','windup')
    a.li('t1',3);a.beq('s4','t1','attack')
    a.li('t1',4);a.beq('s4','t1','recover')
    a.imm(10,'t0','s3',4608);a.beq('t0','zero','idle')
    a.imm(10,'t0','s3',1536);a.bne('t0','zero','begin_windup')
    a.li('t0',2);a.beq('s2','t0','begin_windup')
    a.li('a1',1);a.jal('state_if');a.li('a1',1);a.jal('clip');a.li('a1',16);a.jal('move_toward');a.jump('out')
    a.label('idle');a.li('a1',0);a.jal('state_if');a.li('a1',0);a.jal('clip');a.jump('out')
    a.label('begin_windup');a.li('a1',2);a.jal('state');a.li('t0',1);a.beq('s2','t0','spear_windup')
    a.li('a1',1);a.jump('windup_clip')
    a.label('spear_windup');a.li('a1',2)
    a.label('windup_clip');a.jal('clip');a.jump('out')
    a.label('windup');a.imm(10,'t1','t0',16);a.bne('t1','zero','out')
    a.li('a1',3);a.jal('state');a.li('a1',3);a.jal('clip');a.jump('out')
    a.label('attack')
    a.li('t1',1);a.bne('s2','t1','fan_attack')
    a.li('a1',24);a.jal('move_toward');a.jal('distance');a.move('s3','v0')
    a.label('fan_attack')
    # Only active swing frames damage; GBA's spear body versus forward spear
    # and directional fan range are retained as distinct adapter envelopes.
    a.lw('t0',24,'s1');a.imm(10,'t1','t0',8);a.bne('t1','zero','attack_timer')
    a.imm(10,'t1','t0',28);a.beq('t1','zero','attack_timer')
    a.li('t1',1);a.bne('s2','t1','fan_range')
    a.imm(10,'t1','s3',1152);a.jump('damage_range')
    a.label('fan_range');a.imm(10,'t1','s3',4096)
    a.label('damage_range');a.beq('t1','zero','attack_timer')
    # Vertical separation prevents attacking through terrace floors.
    a.li('t0',0x80069ff8);a.lw('t0',0,'t0');a.lw('t1',20,'s0');a.subu('t0','t0','t1')
    a.addiu('t0','t0',768);a.imm(11,'t1','t0',1536);a.beq('t1','zero','attack_timer')
    a.jal('hurt')
    a.label('attack_timer');a.lw('t0',24,'s1');a.imm(10,'t1','t0',40);a.bne('t1','zero','out')
    a.li('a1',4);a.jal('state');a.li('t0',1);a.bne('s2','t0','fan_recover')
    a.li('a1',4);a.jump('recover_clip')
    a.label('fan_recover');a.li('a1',0)
    a.label('recover_clip');a.jal('clip');a.jump('out')
    a.label('recover');a.imm(10,'t1','t0',64);a.bne('t1','zero','out')
    a.li('a1',0);a.jal('state');a.li('a1',0);a.jal('clip');a.jump('out')

    a.label('dying');a.sw('zero',8,'s0');a.sw('zero',24,'s0')
    a.lw('t0',24,'s1');a.imm(10,'t1','t0',16);a.bne('t1','zero','out')
    a.lw('t0',28,'s1');a.bne('t0','zero','death_wait')
    a.li('t0',1);a.sw('t0',28,'s1');a.li('t0',1);a.beq('s2','t0','spear_death')
    a.li('a1',5);a.jump('death_clip')
    a.label('spear_death');a.li('a1',7)
    a.label('death_clip');a.jal('clip')
    a.label('death_wait');a.lw('t0',24,'s1');a.imm(10,'t1','t0',96);a.bne('t1','zero','out')
    a.move('a0','s0');a.li('a1',0);a.li('a2',0);a.li('a3',0);a.jal(0x80039334)
    a.move('a0','s0');a.jal(0x8004132c)
    # Persist the actual removed-state byte, through the resident memory API.
    a.addiu('a0','s0',72);a.li('a1',1);a.li('a2',0x8006718c);a.jal(0x8004c214);a.jump('out')

    a.label('greeter');a.sw('zero',8,'s0');a.sw('zero',24,'s0');a.li('a1',0);a.jal('clip');a.jump('out')
    # The authored critter bank is installed separately by the content builder.
    # Native health feeding receives original class10 objects from this adapter.
    a.label('critter');a.lbu('t0',72,'s0');a.li('t1',7);a.beq('t0','t1','critter_wait')
    a.lw('t0',24,'s0');a.li('t1',0x00170000);a.r(0x24,'t0','t0','t1');a.sw('zero',24,'s0')
    a.beq('t0','zero','critter_move')
    a.li('a0',0x10);a.move('a1','s0');a.li('t0',0x800670b4);a.lw('t9',0,'t0')
    a.emit(REG['t9']<<21|REG['ra']<<11|9);a.nop()
    a.move('s4','v0');a.sb('zero',72,'s4');a.li('t0',32);a.sb('t0',76,'s4');a.sb('zero',77,'s4')
    a.move('a0','s4');a.li('a1',1);a.jal(0x80041538)
    a.li('a1',7);a.jal('state');a.sw('zero',8,'s0');a.sb('zero',76,'s0');a.jump('out')
    a.label('critter_move')
    # The actual bank has eight authored hop frames. Rotate the rabbit's
    # local heading gradually; the source habitat anchor remains fixed.
    a.lw('t0',20,'s1');a.shift('t0','t0',2);a.sb('t0',70,'s0');a.jump('out')
    a.label('critter_wait');a.sw('zero',8,'s0');a.sb('zero',76,'s0')
    a.lw('t0',24,'s1');a.imm(10,'t1','t0',1200);a.bne('t1','zero','out')
    a.jal('distance');a.imm(10,'t1','v0',6272);a.bne('t1','zero','out')
    a.li('a1',0);a.jal('state');a.li('t0',32);a.sb('t0',76,'s0');a.addiu('t0','s1',64);a.sw('t0',8,'s0');a.jump('out')

    a.label('bell')
    a.lw('s3',24,'s0');a.li('t1',0x00170000);a.r(0x24,'s3','s3','t1');a.sw('zero',24,'s0')
    # Original 0802a038 installs 35 ticks of flame-contact cooldown. A PS1
    # breath supplies multiple live query points over a longer stream, so
    # retain that cooldown while contact continues. One breath must never
    # ring the same bell twice and reset the six-note sequence.
    a.lw('t2',28,'s1');a.subu('t0','t2','s5');a.slt('t1','t0','zero');a.beq('t1','zero','bell_cooldown_nonnegative')
    a.move('t0','zero');a.label('bell_cooldown_nonnegative');a.beq('s3','zero','bell_cooldown_store')
    a.li('t0',35);a.label('bell_cooldown_store');a.sw('t0',28,'s1')
    a.lbu('t0',72,'s0');a.bne('t0','zero','bell_ringing');a.bne('t2','zero','out');a.beq('s3','zero','out')
    a.li('a1',1);a.jal('state');a.li('a1',1);a.jal('clip')
    # Source 0802a038 selects C4/G4/C5 by the bell's source ordinal.
    # The authored bank's three local sound slots point to source-derived
    # SPU samples; play once on accepted collision, not once per frame.
    a.move('a0','s0');a.lw('a1',36,'s1');a.move('a2','zero');a.jal(0x80050648);a.jump('out')
    a.label('bell_ringing');a.sw('zero',24,'s0');a.lw('t0',24,'s1');a.imm(10,'t1','t0',32);a.bne('t1','zero','out')
    a.li('a1',0);a.jal('state');a.li('a1',0);a.jal('clip')
    a.lw('t0',44,'s1');a.lw('s4',0,'t0');a.lw('t0',32,'s4');a.bne('t0','zero','out')
    a.lw('t0',28,'s4');a.move('t1','t0');a.imm(10,'t2','t1',3);a.bne('t2','zero','bell_expected')
    a.addiu('t1','t1',-3)
    a.label('bell_expected');a.li('t2',2);a.subu('t1','t2','t1');a.lw('t2',36,'s1');a.beq('t1','t2','bell_correct')
    a.sw('zero',28,'s4');a.jump('out')
    a.label('bell_correct');a.addiu('t0','t0',1);a.sw('t0',28,'s4');a.li('t1',6);a.bne('t0','t1','out')
    a.li('t0',1);a.sw('t0',32,'s4');a.addiu('a0','s4',32);a.li('a1',4);a.li('a2',0x8006718c);a.jal(0x8004c214);a.jump('out')
    a.label('quest_controller');a.sw('zero',8,'s0');a.sb('zero',76,'s0');a.jump('out')
    a.label('quest_portal');a.sw('zero',8,'s0');a.sw('zero',24,'s0');a.lw('t0',44,'s1');a.lw('t0',0,'t0');a.lw('t0',32,'t0')
    a.bne('t0','zero','portal_open');a.sb('zero',76,'s0');a.jump('out')
    a.label('portal_open');a.li('t0',32);a.sb('t0',76,'s0');a.li('a1',1);a.jal('clip');a.jump('out')

    a.label('attached_fan');a.lw('s4',44,'s1');a.lbu('t0',72,'s4');a.imm(11,'t0','t0',128)
    a.bne('t0','zero','fan_live');a.move('a0','s0');a.jal(0x8004132c);a.jump('out')
    a.label('fan_live')
    for offset in [12,16,20]:a.lw('t0',offset,'s4');a.sw('t0',offset,'s0')
    for offset in [60,61,62,63,64,65,66,68,69,70,76,79,84]:a.lbu('t0',offset,'s4');a.sb('t0',offset,'s0')
    a.sw('zero',8,'s0');a.sw('zero',24,'s0');a.move('a0','s0');a.li('a1',2);a.jal(0x80041538);a.jump('out')
    a.label('out')
    for i,r in enumerate(['ra','s0','s1','s2','s3','s4','s5']):a.lw(r,60-i*4,'sp')
    a.addiu('sp','sp',64);a.jr('ra')

    a.label('distance');a.li('t0',0x80069ff0);a.addiu('a0','s0',12);a.move('a1','t0');a.jump(0x8001bf6c)
    a.label('state_if');a.lbu('t0',72,'s0');a.beq('t0','a1','ret')
    a.label('state');a.sb('a1',72,'s0');a.sw('zero',24,'s1');a.jr('ra')
    a.label('clip');a.lbu('t0',61,'s0');a.beq('t0','a1','ret')
    # S2 uses the same original generic frame clock as the source banks.
    a.sb('a1',60,'s0');a.sb('a1',61,'s0');a.sb('zero',62,'s0');a.li('t0',1);a.sb('t0',63,'s0')
    a.li('t0',0x30);a.sb('t0',64,'s0');a.li('t0',1);a.sb('t0',65,'s0');a.sb('zero',66,'s0')
    # Single-frame idle clips must not leave an active next-frame index 1.
    a.lh('t0',54,'s0');a.emit(REG['t0']<<16|REG['t0']<<11|2<<6);a.li('t1',0x80068c94);a.addu('t0','t0','t1');a.lw('t0',0,'t0')
    a.emit(REG['a1']<<16|REG['t1']<<11|2<<6);a.addu('t0','t0','t1');a.lw('t0',60,'t0');a.lbu('t0',0,'t0');a.imm(11,'t0','t0',2);a.beq('t0','zero','ret')
    a.sb('zero',63,'s0');a.sb('zero',64,'s0')
    a.label('ret');a.jr('ra')
    # The resident Spyro tick clears its hurt word before doing collision.
    # An actor dispatcher runs later, so setting the word alone can be lost
    # on the original CPU. Run the original guarded damage consumer now; the
    # native bridge still consumes the same word through its normal harvest.
    a.label('hurt')
    a.li('t0',0x8006a014);a.lw('t1',0,'t0');a.imm(13,'t1','t1',1);a.sw('t1',0,'t0')
    a.li('t0',0x8006aba0);a.sw('s0',0,'t0');a.jump(0x8002b0b8)
    a.label('move_toward')
    # A bounded approach/lunge around the exact source anchor. Each axis is
    # constrained to 384 units; content validation checks this footprint.
    a.li('t7',0x80069ff0)
    for at,minimum,maximum in [(12,48,52),(16,56,60)]:
        name=f'axis{at}';a.lw('t0',at,'s0');a.lw('t1',at-12,'t7');a.subu('t1','t1','t0');a.slt('t2','t1','zero')
        a.beq('t2','zero',name+'_positive');a.subu('t0','t0','a1');a.jump(name+'_clamp')
        a.label(name+'_positive');a.addu('t0','t0','a1')
        a.label(name+'_clamp');a.lw('t1',minimum,'s1');a.slt('t2','t0','t1');a.bne('t2','zero',name+'_end')
        a.lw('t1',maximum,'s1');a.slt('t2','t1','t0');a.bne('t2','zero',name+'_end');a.sw('t0',at,'s0');a.label(name+'_end')
    # Tail-call keeps the adapter's return address, updating the original grid.
    a.move('a0','s0');a.li('a1',2);a.jump(0x80041538)
    blob=a.finish();code.extend(blob)
    struct.pack_into('<II',code,0x80070478-BASE,2<<26|((entry>>2)&0x3ffffff),0)
    # Original CD13918 transfers complete sectors, while loader70498 places
    # the next scene at this declared size. A short declaration lets the
    # final overlay sector overwrite the following texture descriptors.
    # Padding changes no instruction or branch target.
    padding=(-len(code))%2048;code.extend(bytes(padding))
    return bytes(code),dict(entry=entry,bytes=len(blob),labels=a.labels,
                           declared_overlay_bytes=len(code),sector_padding_bytes=padding,
                           hook='80070478',original_cases_preserved=True,c9_entry=0x80081a64,
                           source='SoI phase3-source callbacks; original YotD class dispatch tables and consumers',
                           limits=['Explicit behavior adapter, not instruction-identical GBA execution.',
                                   'Charge death is a PS1 port addition to the original GBA message-5 flame death.'])
