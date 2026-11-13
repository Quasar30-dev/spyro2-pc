#include "spyro/level.hpp"
#include "spyro/coordinates.hpp"
#include "spyro/yotd_assets.hpp"
#include <array>

namespace spyro {
namespace {
using View=std::span<const std::uint8_t>;
void require(View data,std::size_t at,std::size_t count) {
    if(at>data.size() || count>data.size()-at)throw FormatError("Terrain record outside its scene section");
}
constexpr unsigned tile_size=64,atlas_columns=16;
void texture_atlas(View archive,View descriptors,unsigned count,Level& level,View vram={}) {
    if(vram.empty()) {
        const auto vram_at=read_u32(archive,0);require(archive,vram_at,0x80000);
        vram=archive.subspan(vram_at,0x80000);
    } else if(vram.size()!=0x80000)throw FormatError("Invalid terrain VRAM override");
    auto word=[&](unsigned x,unsigned y) {
        if(x<512 || x>=1024 || y>=512)throw FormatError("Terrain material references unloaded VRAM");
        return read_u16(vram,(static_cast<std::size_t>(y)*512+x-512)*2);
    };
    level.texture_width=atlas_columns*tile_size;
    level.texture_height=((count+atlas_columns-1)/atlas_columns)*tile_size;
    level.texture_rgba.resize(static_cast<std::size_t>(level.texture_width)*level.texture_height*4);
    level.terrain_materials.resize(count);
    for(unsigned material=0;material<count;++material) {
        // Six 8-byte descriptors: two distant textures, four 32x32 near
        // quadrants. Assemble the near quadrants into one native 64x64 tile.
        for(unsigned quadrant=0;quadrant<4;++quadrant) {
            const auto descriptor=descriptors.subspan((material*6+2+quadrant)*8,8);
            const unsigned page=descriptor[6],flags=descriptor[7],clut=read_u16(descriptor,2);
            // 80024f38 / 80025384: descriptor bit 31 selects GPU commands
            // 3e / 36 (semi-transparent). TPAGE bits 5..6 choose the blend.
            // The four near quadrants agree in all 41 retail scene banks.
            const TerrainMaterial blend{(flags&0x80)!=0,(page>>5)&3};
            auto& target=level.terrain_materials[material];
            if(quadrant && (target.translucent!=blend.translucent || target.blend_mode!=blend.blend_mode))
                throw FormatError("Terrain quadrants have different blend modes");
            target=blend;
            const unsigned left=descriptor[0],top=descriptor[1],right=descriptor[4],bottom=descriptor[5];
            if(right<left || bottom<top)throw FormatError("Invalid terrain UV rectangle");
            for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
                unsigned sx=x,sy=y;
                // Texture-level rotation and reflection are separate from the
                // triangle's missing-corner selector in its face flags.
                for(unsigned turn=0;turn<((flags>>4)&3);++turn) {const auto previous=sx;sx=31-sy;sy=previous;}
                if(flags&0x40)std::swap(sx,sy);
                const unsigned u=left+sx*(right-left)/31,v=top+sy*(bottom-top)/31;
                const unsigned depth=(page>>7)&1,pixels_per_word=depth?2:4;
                const unsigned packed=word((page&15)*64+u/pixels_per_word,((page>>4)&1)*256+v);
                const unsigned index=(packed>>((u%pixels_per_word)*(depth?8:4)))&(depth?255:15);
                const unsigned color=word((clut&63)*16+index,(clut>>6)&511);
                const auto px=(material%atlas_columns)*tile_size+(quadrant%2)*32+x;
                const auto py=(material/atlas_columns)*tile_size+(quadrant/2)*32+y;
                const auto at=(static_cast<std::size_t>(py)*level.texture_width+px)*4;
                for(unsigned c=0;c<3;++c) {
                    const auto value=(color>>(c*5))&31;
                    level.texture_rgba[at+c]=static_cast<std::uint8_t>((value<<3)|(value>>2));
                }
                // Preserve CLUT bit 15 independently of the primitive flag.
                // In additive water, 0x8000 is black STP and leaves the
                // background visible; it must not become opaque black.
                level.texture_rgba[at+3]=color?((color&0x8000)?128:255):0;
            }
        }
    }
}
void region_geometry(View regions,unsigned texture_count,Level& level) {
    // Loader 8006fa98 relocates this table relative to section+4, not the WAD.
    const std::size_t directory=4+read_u32(regions,4);
    const unsigned count=read_u32(regions,directory);
    if(count>4096)throw FormatError("Invalid terrain region count");
    require(regions,directory+4,static_cast<std::size_t>(count)*4);level.terrain_regions=count;
    for(unsigned region=0;region<count;++region) {
        const std::size_t at=4+read_u32(regions,directory+4+region*4);require(regions,at,28);
        const unsigned flags=read_u16(regions,at+4);
        const unsigned vertices=regions[at+20],colors=regions[at+21],faces=regions[at+22],start=regions[at+23];
        if(!vertices || !faces || (flags&0x4000))continue;
        if(!colors)throw FormatError("Terrain region has no vertex colors");
        const std::size_t vertex_at=at+28+start*4,color_at=vertex_at+vertices*4,face_at=color_at+colors*8;
        require(regions,vertex_at,vertices*4+colors*8+faces*16);
        const Vec3 origin{read_u16(regions,at+10)*16.0f,read_u16(regions,at+8)*16.0f,read_u16(regions,at+14)*16.0f};
        const float vertical_scale=(flags&0x1000)?2.0f:16.0f;
        std::vector<Vec3> positions;positions.reserve(vertices);
        for(unsigned i=0;i<vertices;++i) {
            const auto packed=read_u32(regions,vertex_at+i*4);
            // 80024664 and 80028be4: unsigned 11/11/10-bit packed coordinates.
            positions.push_back(retail::world_position(origin+Vec3{(packed>>21)*16.0f,((packed>>10)&2047)*16.0f,((packed&1023)*2)*vertical_scale}));
        }
        for(unsigned f=0;f<faces;++f) {
            const auto face=regions.subspan(face_at+f*16,16);
            const bool triangle=face[0]==face[1];
            const unsigned material=face[12]&127,rotation=(face[13]>>4)&3;
            if(material>=texture_count)throw FormatError("Terrain face references an invalid material");
            // 80024a00/80024c60: byte order 3,2,0,1 is the GPU strip.
            const unsigned quad_order[]={3,2,0,0,2,1},triangle_order[]={3,2,1};
            constexpr std::array<std::array<float,2>,4> strip_uv={{{0,0},{1,0},{0,1},{1,1}}};
            for(unsigned corner=0;corner<(triangle?3u:6u);++corner) {
                const auto index=triangle?triangle_order[corner]:quad_order[corner];
                const auto vi=face[index],ci=face[4+index];
                if(vi>=vertices || ci>=colors)throw FormatError("Terrain face has an invalid vertex or color index");
                const auto rgb=read_u32(regions,color_at+ci*4);
                const Vec3 color{static_cast<float>(rgb&255)/128.0f,static_cast<float>((rgb>>8)&255)/128.0f,static_cast<float>((rgb>>16)&255)/128.0f};
                const unsigned uv_index=triangle?corner:(index==3?0u:(index==2?1u:(index==0?2u:3u)));
                auto uv=strip_uv[uv_index];
                if(triangle)for(unsigned r=0;r<rotation;++r){const auto old=uv[0];uv[0]=1-uv[1];uv[1]=old;}
                const float u=((material%atlas_columns)*tile_size+0.5f+uv[0]*(tile_size-1))/level.texture_width;
                const float v=((material/atlas_columns)*tile_size+0.5f+uv[1]*(tile_size-1))/level.texture_height;
                level.visual_vertices.push_back({positions[vi],color,u,v,material,region,vi,ci});
            }
            ++level.terrain_faces;
        }
    }
}
}
void decode_terrain(View archive,Level& level) {
    const auto scene_at=read_u32(archive,8),scene_size=read_u32(archive,12);
    require(archive,scene_at,scene_size);const auto scene=archive.subspan(scene_at,scene_size);
    const auto texture_bytes=read_u32(scene,0),texture_count=read_u32(scene,4);
    if(!texture_count || texture_count>256 || texture_bytes!=8+texture_count*48)
        throw FormatError("Invalid terrain texture directory");
    require(scene,8,texture_count*48);texture_atlas(archive,scene.subspan(8,texture_count*48),texture_count,level);
    const std::size_t chunk=texture_bytes,chunk_size=read_u32(scene,chunk);
    require(scene,chunk,chunk_size);region_geometry(scene.subspan(chunk,chunk_size),texture_count,level);
}
void decode_terrain(View archive,Level& level,const GameProfile& profile,unsigned area) {
    if(!profile.yotd()) {
        if(area)throw FormatError("Spyro 2 terrain has no YOTD subareas");
        decode_terrain(archive,level);return;
    }
    if(area>=level_area_count(archive,profile))throw FormatError("YOTD terrain subarea is not present");
    const auto children=parse_level_archive(archive,profile);
    const auto scene=archive.subspan(children[1].offset,children[1].size);
    const auto texture_bytes=read_u32(scene,0),texture_count=read_u32(scene,4);
    if(!texture_count || texture_count>256 || texture_bytes!=8+texture_count*48)
        throw FormatError("Invalid YOTD terrain texture directory");
    require(scene,8,texture_count*48);
    const auto vram=decode_yotd_vram(archive,profile,area);
    texture_atlas(archive,scene.subspan(8,texture_count*48),texture_count,level,vram.pixels);
    if(!area) {
        const auto size=read_u32(scene,texture_bytes);require(scene,texture_bytes,size);
        region_geometry(scene.subspan(texture_bytes,size),texture_count,level);return;
    }
    // Each extra-area blob carries model data, its 0x180-byte model/class
    // directory, then an ordinary terrain region chunk. Texture descriptors
    // and the VRAM image belong to the parent level and are shared by areas.
    const auto entry=0x20u+(area-1)*16;
    const auto at=read_u32(archive,entry),bytes=read_u32(archive,entry+4);
    require(archive,at,bytes);const auto blob=archive.subspan(at,bytes);
    const std::size_t regions=std::size_t(read_u32(blob,0))+0x180;
    const auto size=read_u32(blob,regions);require(blob,regions,size);
    region_geometry(blob.subspan(regions,size),texture_count,level);
}
} // namespace spyro
