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
std::vector<std::uint8_t> convert_xbox_tiled_bc_mips_to_dds(
    std::span<const XboxTiledBcMip> mips, bool bc3) {
    if(mips.empty()||mips.size()>15)
        throw std::invalid_argument("Invalid DDS mip count");
    const std::size_t bytes_per_block=bc3?16u:8u;
    constexpr std::size_t kMaxDdsBytes=384u*1024u*1024u;
    std::size_t total=128,top_linear_bytes=0;
    // Validate all dimensions and the allocation bound before decoding.
    for(std::size_t i=0;i<mips.size();++i){
        const auto& m=mips[i];
        if(!m.width||!m.height||m.width>16384||m.height>16384)
            throw std::invalid_argument("Invalid Xbox 360 BC texture dimensions");
        if(i){
            const auto& prev=mips[i-1];
            if((prev.width==1&&prev.height==1)||
               m.width!=std::max(1u,prev.width/2)||
               m.height!=std::max(1u,prev.height/2))
                throw std::invalid_argument("Noncontiguous DDS mip dimensions");
        }
        const std::size_t blocks_w=(m.width+3u)/4u,blocks_h=(m.height+3u)/4u;
        const std::size_t linear_bytes=blocks_w*blocks_h*bytes_per_block;
        if(linear_bytes>256u*1024u*1024u||linear_bytes>kMaxDdsBytes-total)
            throw std::runtime_error("Unreasonably large Xbox 360 texture mip chain");
        if(!i)top_linear_bytes=linear_bytes;
        total+=linear_bytes;
    }
    std::vector<std::uint8_t> result(total,0);
    result[0]='D';result[1]='D';result[2]='S';result[3]=' ';
    little32(result,4,124);
    little32(result,8,0x81007u|(mips.size()>1?0x20000u:0u));
    little32(result,12,mips.front().height);
    little32(result,16,mips.front().width);
    little32(result,20,std::uint32_t(top_linear_bytes));
    if(mips.size()>1)little32(result,28,std::uint32_t(mips.size()));
    little32(result,76,32);
    little32(result,80,4);
    result[84]='D';result[85]='X';result[86]='T';result[87]=bc3?'5':'1';
    little32(result,108,0x1000u|(mips.size()>1?0x400008u:0u));
    std::size_t dst_mip=128;
    for(const auto& m:mips){
        const std::uint32_t block_w=(m.width+3u)/4u,block_h=(m.height+3u)/4u;
        const std::uint32_t tiled_w=(std::max(32u,block_w)+31u)&~31u;
        // Xenos DXT words have the opposite 16-bit byte order to a PC DDS.
        for(std::uint32_t y=0;y<block_h;++y){
            for(std::uint32_t x=0;x<block_w;++x){
                const std::uint64_t block=tiled_offset(x,y,tiled_w,bc3?4:3);
                const std::uint64_t src=block*bytes_per_block;
                if(src>m.payload.size()||bytes_per_block>m.payload.size()-src)
                    throw std::runtime_error("GPU-tiled BC mip references missing data");
                const std::size_t dst=dst_mip+(std::size_t(y)*block_w+x)*bytes_per_block;
                for(std::size_t b=0;b<bytes_per_block;b+=2){
                    result[dst+b]=m.payload[src+b+1];
                    result[dst+b+1]=m.payload[src+b];
                }
            }
        }
        dst_mip+=std::size_t(block_w)*block_h*bytes_per_block;
    }
    return result;
}
std::vector<std::uint8_t> convert_xbox_tiled_bc_to_dds(
    std::uint32_t width, std::uint32_t height,bool bc3,
    std::span<const std::uint8_t> xenos_payload) {
    const XboxTiledBcMip mip{width,height,xenos_payload};
    return convert_xbox_tiled_bc_mips_to_dds(std::span<const XboxTiledBcMip>(&mip,1),bc3);
}
std::vector<Ue3Mip> select_resident_ue3_mip_chain(
    std::span<const Ue3Mip> mips) {
    std::vector<Ue3Mip> selected;
    selected.reserve(std::min<std::size_t>(mips.size(),15));
    for(const auto& mip:mips){
        const bool resident=mip.storage==Ue3MipStorage::Embedded||
                            mip.storage==Ue3MipStorage::LzoEmbedded;
        if(!resident){
            if(!selected.empty())break;
            continue;
        }
        if(!selected.empty()){
            const auto& prev=selected.back();
            if((prev.width==1&&prev.height==1)||
               mip.width!=std::max(1u,prev.width/2)||
               mip.height!=std::max(1u,prev.height/2))
                break;
        }
        selected.push_back(mip);
        if(selected.size()==15)break;
    }
    return selected;
}
std::optional<std::vector<std::uint8_t>> export_ue3_texture_dds(
    const Ue3Package&pkg,std::size_t idx){
    const auto texture=inspect_ue3_texture2d(pkg,idx);
    if(texture.format!="PF_DXT1"&&texture.format!="PF_DXT5")
        return std::nullopt;
    const bool bc3=(texture.format=="PF_DXT5");
    const auto parsed=parse_ue3_texture_mips(pkg,idx);
    const auto selected=select_resident_ue3_mip_chain(parsed);
    if(selected.empty())return std::nullopt;
    // Bound decoded source allocations before reading embedded or LZO data.
    constexpr std::size_t kMaxResidentBytes=384u*1024u*1024u;
    std::size_t decoded_total=0;
    for(const auto& mip:selected){
        if(mip.element_count>kMaxResidentBytes-decoded_total)
            throw std::runtime_error("Resident UE3 mip chain exceeds DDS size limit");
        decoded_total+=mip.element_count;
    }
    std::vector<std::vector<std::uint8_t>> payloads;
    payloads.reserve(selected.size());
    for(const auto& mip:selected)
        payloads.push_back(read_ue3_texture_mip_bytes(pkg,idx,mip));
    std::vector<XboxTiledBcMip> views;
    views.reserve(selected.size());
    for(std::size_t i=0;i<selected.size();++i)
        views.push_back({selected[i].width,selected[i].height,payloads[i]});
    return convert_xbox_tiled_bc_mips_to_dds(views,bc3);
}
}
