#include "spyro/overlay_vm.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
using namespace spyro;
static std::vector<std::uint8_t> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static unsigned number(const std::string& s){return static_cast<unsigned>(std::stoul(s,nullptr,0));}
int main(int argc,char** argv){try{
 struct Call {unsigned entry{};std::vector<std::uint32_t> args;bool explicit_args=false;};
 OverlayVm vm;vm.r[29]=0x807fe000;std::uint64_t budget=2000000;std::vector<Call> calls;std::vector<std::uint32_t> args;
 std::vector<std::pair<unsigned,unsigned>> peeks,loads;std::filesystem::path save_ram;bool continue_on_fault=false,call_fault=false;
 for(int i=1;i<argc;i++){std::string a=argv[i];
  if(a=="--exe"){auto b=read(argv[++i]);unsigned base=0,size=0;std::copy_n(b.begin()+0x18,4,reinterpret_cast<unsigned char*>(&base));std::copy_n(b.begin()+0x1c,4,reinterpret_cast<unsigned char*>(&size));vm.load(base,std::span(b).subspan(0x800,size));}
  else if(a=="--load"){auto base=number(argv[++i]);auto b=read(argv[++i]);vm.load(base,b);loads.push_back({base,static_cast<unsigned>(b.size())});auto d=find_dispatcher(b,base);std::printf("load %08x size %zx dispatcher site %08x entry %08x cases %zu reg %u\n",base,b.size(),d.site,d.entry,d.cases.size(),d.moby_register);}
  else if(a=="--ram"){auto b=read(argv[++i]);if(b.size()!=OverlayVm::ram_size)throw std::runtime_error("RAM snapshot must be exactly 8 MB");vm.load(0x80000000,b);}
  else if(a=="--save-ram")save_ram=argv[++i];
  else if(a=="--continue-on-fault")continue_on_fault=true;
  else if(a=="--call")calls.push_back({number(argv[++i]),{},false});
  else if(a=="--invoke"){Call call;call.entry=number(argv[++i]);const auto count=number(argv[++i]);if(count>4)throw std::runtime_error("At most four register arguments are supported");for(unsigned k=0;k<count;++k)call.args.push_back(number(argv[++i]));call.explicit_args=true;calls.push_back(std::move(call));}
  else if(a=="--arg")args.push_back(number(argv[++i]));
  else if(a=="--write"){auto at=number(argv[++i]);vm.write32(at,number(argv[++i]));}
  else if(a=="--watch")vm.watch=number(argv[++i]);
  else if(a=="--budget")budget=std::stoull(argv[++i]);
  else if(a=="--guard"){auto begin=number(argv[++i]),end=number(argv[++i]);for(auto p=begin;p<end;p+=4)vm.guard[p]=true;}
  else if(a=="--peek"){auto begin=number(argv[++i]),end=number(argv[++i]);peeks.push_back({begin,end});}
 }
 std::unordered_map<std::uint32_t,std::uint64_t> trace;vm.trace=&trace;
 for(const auto& call:calls){const auto before=vm.executed;auto values=call.explicit_args?std::span<const std::uint32_t>(call.args):std::span<const std::uint32_t>(args);auto result=vm.call(call.entry,values,budget);std::printf("call %08x result %08x instructions %llu fault %s pc %08x\n",call.entry,result,static_cast<unsigned long long>(vm.executed-before),vm.fault.c_str(),vm.pc);for(unsigned k=0;k<16;k++)std::printf("history %08x\n",vm.history[(vm.history_at+k)&15]);if(!vm.fault.empty()){call_fault=true;if(!continue_on_fault)break;}}
 for(auto [begin,end]:peeks)for(auto p=begin;p<end;p+=4)std::printf("%08x %08x\n",p,vm.read32(p));
 for(auto [base,size]:loads){auto bytes=std::span(vm.ram).subspan(base&0x1fffffff,size);auto d=find_dispatcher(bytes,base);std::printf("postcall S2 case-tree candidate site %08x entry %08x cases %zu reg %u\n",d.site,d.entry,d.cases.size(),d.moby_register);auto y=find_yotd_moby_handlers(bytes,base);std::printf("postcall YOTD class table %08x handlers %zu\n",y.table,y.updates.size());for(const auto& [type,entry]:y.updates)std::printf("class %03x update %08x\n",type,entry);}
 if(!save_ram.empty()){std::ofstream out(save_ram,std::ios::binary);out.write(reinterpret_cast<const char*>(vm.ram.data()),static_cast<std::streamsize>(vm.ram.size()));if(!out)throw std::runtime_error("RAM snapshot write failed");}
 std::vector<std::pair<std::uint32_t,std::uint64_t>> targets(trace.begin(),trace.end());std::sort(targets.begin(),targets.end(),[](auto a,auto b){return a.second>b.second;});for(auto [a,n]:targets)std::printf("target %08x %llu\n",a,static_cast<unsigned long long>(n));
 for(auto [a,n]:vm.skipped)std::printf("skipped %08x %llu\n",a,static_cast<unsigned long long>(n));
 return call_fault?2:0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
