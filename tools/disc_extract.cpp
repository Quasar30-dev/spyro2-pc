#include "spyro/assets.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

// Uses the same bounded ISO9660/Mode2 reader as the workbench. Streaming XA
// files retain complete sectors beside their logical filesystem extraction.
int main(int argc,char** argv) {
    try {
        if(argc!=3){std::cerr<<"Usage: spyro_disc_extract <CUE|BIN|ISO> <new-output-directory>\n";return 2;}
        spyro::AssetStore disc(std::filesystem::u8path(argv[1]));
        const auto output=std::filesystem::absolute(std::filesystem::u8path(argv[2]));
        if(std::filesystem::exists(output) && !std::filesystem::is_empty(output))throw spyro::FormatError("Extraction destination must be empty");
        std::filesystem::create_directories(output);
        std::ofstream manifest(output/"disc-extents.tsv");
        manifest<<"path\tlba\tlogical_bytes\tcrc32\traw_sectors\n";
        for(const auto& [name,file]:disc.files()) {
            const auto target=output/std::filesystem::u8path(name);
            std::filesystem::create_directories(target.parent_path());
            std::ofstream stream(target,std::ios::binary);
            if(!stream)throw spyro::FormatError("Cannot create extracted file");
            const bool xa=target.extension()==".STR" || target.extension()==".XA";
            bool raw=false;
            if(xa) {
                // Some STR files are Form1, others mix XA Form2 records. The
                // .raw sidecar is authoritative for streaming audio either way.
                std::ofstream sectors(target.string()+".raw",std::ios::binary);
                for(std::uint64_t i=0;i<(file.size+2047)/2048;++i) {
                    const auto bytes=disc.read_raw_sector(file.lba+static_cast<std::uint32_t>(i));
                    sectors.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                }
                if(!sectors)throw spyro::FormatError("Raw extraction write failed");
                raw=true;
            }
            std::uint32_t crc=0xffffffff;
            for(std::uint64_t at=0;at<file.size;) {
                const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(file.size-at,1024*1024));
                spyro::Bytes bytes;
                try {bytes=disc.read(name,at,n);}
                catch(const spyro::FormatError&) {
                    if(!raw)throw;
                    // A logical ISO extraction cannot retain Form2's 2324
                    // payload bytes; store its 2048-byte filesystem extent and
                    // leave all original sector bytes in the .raw sidecar.
                    bytes.reserve(n);
                    for(std::uint64_t off=at;off<at+n;off+=2048) {
                        const auto sector=disc.read_raw_sector(file.lba+static_cast<std::uint32_t>(off/2048));
                        const unsigned start=sector.size()==2048?0:sector[15]==1?16:24;
                        const auto take=std::min<std::size_t>(2048,at+n-off);
                        bytes.insert(bytes.end(),sector.begin()+start,sector.begin()+start+take);
                    }
                }
                stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                for(auto b:bytes){crc^=b;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
                at+=n;
            }
            if(!stream)throw spyro::FormatError("Extraction write failed");
            manifest<<name<<'\t'<<file.lba<<'\t'<<file.size<<'\t'<<std::hex<<std::setw(8)<<std::setfill('0')<<~crc<<std::dec<<'\t'<<raw<<'\n';
            std::cout<<name<<" LBA="<<file.lba<<" bytes="<<file.size<<" CRC32="<<std::hex<<~crc<<std::dec<<'\n';
        }
        if(!manifest)throw spyro::FormatError("Manifest write failed");
        return 0;
    } catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 1;}
}
