#include "ue3_dds.hpp"
#include "ue3_mip.hpp"
#include "ue3_texture.hpp"
#include <algorithm>
#include <stdexcept>

namespace dotm {
namespace {
std::uint32_t tiled_offset(std::uint32_t x,std::uint32_t y,
                           std::uint32_t width,unsigned log_bpb) {
    const std::uint32_t aligned=(width+31u)&~31u;
    const auto macro=((x>>5)+(y>>5)*(aligned>>5))<<(log_bpb+7);
    const auto micro=((x&7u)+((y&14u)<<2))<<log_bpb;
    const auto off=macro+((micro&~15u)<<1)+(micro&15u)+((y&1u)<<4);
    return (((off&~511u)<<3)+((y&16u)<<7)+((off&448u)<<2)+
            (((((y&8u)>>2)+(x>>3))&3u)<<6)+(off&63u))>>log_bpb;
}
void little32(std::vector<std::uint8_t>&v,std::size_t at,std::uint32_t value){
    if(at>v.size()||v.size()-at<4)throw std::logic_error("DDS output header bounds");
    for(int b=0;b<4;++b)v[at+b]=std::uint8_t(value>>(8*b));
}
}
std::vector<std::uint8_t> convert_xbox_tiled_bc_to_dds(
    std::uint32_t width, std::uint32_t height,bool bc3,
    std::span<const std::uint8_t> xenos_payload) {
    if(!width||!height||width>16384||height>16384)
        throw std::invalid_argument("Invalid Xbox 360 BC texture dimensions");
    const std::size_t bytes_per_block=bc3?16u:8u;
    const std::uint32_t block_w=(width+3)/4,block_h=(height+3)/4;
    const std::uint32_t tiled_w=(std::max(32u,block_w)+31u)&~31u;
    const std::size_t linear_bytes=std::size_t(block_w)*block_h*bytes_per_block;
    if(linear_bytes>256u*1024u*1024u)
        throw std::runtime_error("Unreasonably large Xbox 360 texture");
    std::vector<std::uint8_t> result(128+linear_bytes,0);
    result[0]='D';result[1]='D';result[2]='S';result[3]=' ';
    little32(result,4,124);      // DDS_HEADER size.
    little32(result,8,0x81007);  // CAPS | HEIGHT | WIDTH | PIXELFORMAT | LINEARSIZE.
    little32(result,12,height);
    little32(result,16,width);
    little32(result,20,std::uint32_t(linear_bytes));
    little32(result,76,32);      // DDS_PIXELFORMAT size.
    little32(result,80,4);       // DDPF_FOURCC.
    result[84]='D';result[85]='X';result[86]='T';result[87]=bc3?'5':'1';
    little32(result,108,0x1000); // DDSCAPS_TEXTURE.
    // Untile at BC blocks (4x4 pixels), applying Xenos 2D tile addresses.
    // Xenos DXT words have the opposite 16-bit byte order to a PC DDS.
    for(std::uint32_t y=0;y<block_h;++y){
        for(std::uint32_t x=0;x<block_w;++x){
            const std::uint64_t block=tiled_offset(x,y,tiled_w,bc3?4:3);
            const std::uint64_t src=block*bytes_per_block;
            if(src>xenos_payload.size()||bytes_per_block>xenos_payload.size()-src)
                throw std::runtime_error("GPU-tiled BC mip references missing data");
            const std::size_t dst=128+(std::size_t(y)*block_w+x)*bytes_per_block;
            for(std::size_t b=0;b<bytes_per_block;b+=2){
                result[dst+b]=xenos_payload[src+b+1];
                result[dst+b+1]=xenos_payload[src+b];
            }
        }
    }
    return result;
}
std::optional<std::vector<std::uint8_t>> export_ue3_texture_dds(
    const Ue3Package&pkg,std::size_t idx){
    const auto texture=inspect_ue3_texture2d(pkg,idx);
    if(texture.format!="PF_DXT1"&&texture.format!="PF_DXT5")
        return std::nullopt;
    const bool bc3=(texture.format=="PF_DXT5");
    for(const auto& mip:parse_ue3_texture_mips(pkg,idx)){
        if(mip.storage!=Ue3MipStorage::Embedded&&
           mip.storage!=Ue3MipStorage::LzoEmbedded)continue;
        const auto bytes=read_ue3_texture_mip_bytes(pkg,idx,mip);
        return convert_xbox_tiled_bc_to_dds(mip.width,mip.height,bc3,bytes);
    }
    return std::nullopt;
}
}