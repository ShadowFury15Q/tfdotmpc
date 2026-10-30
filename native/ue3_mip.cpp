#include "ue3_mip.hpp"
#include "ue3_properties.hpp"
#include <lzokay.hpp>
#include <algorithm>
#include <span>
#include <stdexcept>

namespace dotm {
namespace {
constexpr std::uint32_t kMagic=0x9E2A83C1u;
constexpr std::size_t kMaxMip=64u*1024u*1024u;
std::uint32_t be(std::span<const std::uint8_t> d,std::size_t p) {
    if(p>d.size()||d.size()-p<4)throw std::runtime_error("Truncated DOTM mip header");
    return (std::uint32_t(d[p])<<24)|(std::uint32_t(d[p+1])<<16)|
           (std::uint32_t(d[p+2])<<8)|std::uint32_t(d[p+3]);
}
void checked(std::span<const std::uint8_t> d,std::size_t at,std::size_t n) {
    if(at>d.size()||n>d.size()-at)throw std::runtime_error("DOTM mip exceeds UObject data");
}
std::vector<std::uint8_t> expand_lzo(std::span<const std::uint8_t> input,std::uint32_t outsize){
    if(outsize>kMaxMip||input.size()<24||be(input,0)!=kMagic)
        throw std::runtime_error("Invalid DOTM LZO-compressed texture mip");
    const std::uint32_t blocksize=be(input,4),stored=be(input,8),raw=be(input,12);
    if(blocksize==0||blocksize>1048576||raw!=outsize)
        throw std::runtime_error("Invalid DOTM texture LZO block geometry");
    const std::size_t blocks=(std::size_t(raw)+blocksize-1)/blocksize;
    if(blocks>4096)throw std::runtime_error("Too many compressed texture blocks");
    checked(input,16,8*blocks);
    // Validate the entire block table and compressed payload before
    // allocating the uncompressed buffer or calling the LZO decoder.
    const std::size_t data_start=16+8*blocks;
    std::size_t scan_in=data_start,scan_out=0,comp_sum=0;
    for(std::size_t j=0;j<blocks;++j){
        const auto comp=be(input,16+j*8),expanded=be(input,20+j*8);
        if(!comp||!expanded||expanded>blocksize)
            throw std::runtime_error("Invalid compressed texture sub-block");
        checked(input,scan_in,comp);
        if(expanded>std::size_t(outsize)-scan_out)
            throw std::runtime_error("Overlong texture mip decompression");
        scan_in+=comp;scan_out+=expanded;comp_sum+=comp;
    }
    if(stored!=comp_sum||scan_out!=outsize||scan_in!=input.size())
        throw std::runtime_error("Compressed DOTM mip length mismatch");
    std::vector<std::uint8_t> result(outsize);
    std::size_t inpos=data_start,outpos=0;
    for(std::size_t j=0;j<blocks;++j){
        const auto comp=be(input,16+j*8),expanded=be(input,20+j*8);
        std::size_t written=0;
        auto rc=lzokay::decompress(input.data()+inpos,comp,
                                  result.data()+outpos,expanded,written);
        if(rc!=lzokay::EResult::Success||written!=expanded)
            throw std::runtime_error("DOTM LZO texture mip decoding failed");
        inpos+=comp;outpos+=expanded;
    }
    return result;
}
}
std::vector<Ue3Mip> parse_ue3_texture_mips(const Ue3Package&pkg,std::size_t idx){
    if(pkg.export_class_path(idx)!="Engine.Texture2D")
        throw std::invalid_argument("DOTM export is not Engine.Texture2D");
    auto props=parse_ue3_tagged_properties(pkg,idx);
    auto bytes=pkg.serialized_object(idx);
    auto p=props.native_data_offset;
    auto n=be(bytes,p);p+=4;
    if(n>24)throw std::runtime_error("Unreasonable DOTM mipmap count");
    std::vector<Ue3Mip> levels;
    levels.reserve(n);
    for(std::uint32_t i=0;i<n;++i){
        checked(bytes,p,20);
        Ue3Mip m;
        m.flags=be(bytes,p);
        m.element_count=be(bytes,p+4);
        m.stored_size=be(bytes,p+8);
        m.source_offset=be(bytes,p+12);
        m.opaque_word=be(bytes,p+16);
        p+=20;
        switch(m.flags){
        case 0x00:
            m.storage=Ue3MipStorage::Embedded;break;
        case 0x10:
            m.storage=Ue3MipStorage::LzoEmbedded;break;
        case 0x11:
            m.storage=Ue3MipStorage::External;break;
        case 0x21:
            m.storage=Ue3MipStorage::Absent;break;
        default:
            throw std::runtime_error("Unsupported DOTM mip bulk flag");
        }
        if(m.storage==Ue3MipStorage::Embedded||m.storage==Ue3MipStorage::LzoEmbedded){
            if(m.stored_size>kMaxMip||m.element_count>kMaxMip)
                throw std::runtime_error("DOTM embedded mip exceeds size limit");
            const auto file_offset=std::uint64_t(pkg.exports()[idx].serial_offset)+p;
            if(m.source_offset!=file_offset)
                throw std::runtime_error("Embedded DOTM mip offset mismatch");
            m.object_data_offset=p;
            checked(bytes,p,m.stored_size);
            if(m.storage==Ue3MipStorage::Embedded && m.stored_size!=m.element_count)
                throw std::runtime_error("Uncompressed texture mip length mismatch");
            p+=m.stored_size;
        }else if(m.storage==Ue3MipStorage::Absent){
            // Sentinel bulk references have no data in this package.
            if(m.element_count!=0||m.stored_size!=0xffffffffu||
               m.source_offset!=0xffffffffu)
                throw std::runtime_error("Unexpected empty DOTM mip bulk descriptor");
        }
        checked(bytes,p,8);
        m.width=be(bytes,p);
        m.height=be(bytes,p+4);
        p+=8;
        if(m.width==0||m.height==0||m.width>32768||m.height>32768)
            throw std::runtime_error("Invalid DOTM mip dimensions");
        levels.push_back(m);
    }
    if(p!=bytes.size())
        throw std::runtime_error("Unparsed bytes after DOTM Texture2D mip chain");
    return levels;
}
std::vector<std::uint8_t> read_ue3_texture_mip_bytes(
    const Ue3Package&pkg,std::size_t idx,const Ue3Mip&m){
    if(m.storage==Ue3MipStorage::External||m.storage==Ue3MipStorage::Absent)
        throw std::runtime_error("DOTM texture mip bytes are not present in this package");
    const auto bytes=pkg.serialized_object(idx);
    checked(bytes,m.object_data_offset,m.stored_size);
    const auto source=bytes.subspan(m.object_data_offset,m.stored_size);
    if(m.storage==Ue3MipStorage::LzoEmbedded)
        return expand_lzo(source,m.element_count);
    return {source.begin(),source.end()};
}
} // namespace dotm
