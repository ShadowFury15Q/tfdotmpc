#include "ue3_texture.hpp"
#include "ue3_properties.hpp"
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace dotm {
namespace {
std::int32_t get_i32(const Ue3Package&pkg,std::size_t idx,
                     const Ue3PropertyStream&tags,std::string_view name) {
    for(const auto& tag:tags.properties) {
        if(tag.name == name && tag.type == "IntProperty")
            return ue3_int_property(ue3_property_payload(pkg,idx,tag));
    }
    throw std::runtime_error("Missing DOTM Texture2D IntProperty: "+std::string(name));
}
std::string get_format(const Ue3Package&pkg,std::size_t idx,const Ue3PropertyStream&tags) {
    for(const auto& tag:tags.properties) {
        if(tag.name!="Format" || tag.type!="ByteProperty")continue;
        const auto b=ue3_property_payload(pkg,idx,tag);
        if(b.size()!=8)throw std::runtime_error("Unexpected Texture2D format FName length");
        const std::uint32_t name=(std::uint32_t(b[0])<<24)|(std::uint32_t(b[1])<<16)|
                                 (std::uint32_t(b[2])<<8)|std::uint32_t(b[3]);
        if(name>=pkg.names().size())throw std::runtime_error("Invalid Texture2D format FName");
        return pkg.names()[name];
    }
    throw std::runtime_error("Missing DOTM Texture2D format property");
}
}
Ue3TextureInfo inspect_ue3_texture2d(const Ue3Package&pkg,std::size_t idx) {
    if(pkg.export_class_path(idx)!="Engine.Texture2D")
        throw std::invalid_argument("Not an Engine.Texture2D asset");
    auto stream=parse_ue3_tagged_properties(pkg,idx);
    const auto serialized=pkg.serialized_object(idx);
    Ue3TextureInfo result;
    result.object_path=pkg.object_reference_path(std::int32_t(idx+1));
    result.width=get_i32(pkg,idx,stream,"SizeX");
    result.height=get_i32(pkg,idx,stream,"SizeY");
    result.original_width=get_i32(pkg,idx,stream,"OriginalSizeX");
    result.original_height=get_i32(pkg,idx,stream,"OriginalSizeY");
    result.mip_tail_base_index=get_i32(pkg,idx,stream,"MipTailBaseIdx");
    result.format=get_format(pkg,idx,stream);
    if(result.width<1||result.height<1||result.width>32768||result.height>32768||
       result.original_width<1||result.original_height<1||
       result.original_width>32768||result.original_height>32768)
        throw std::runtime_error("Invalid DOTM Texture2D dimensions");
    result.native_payload_offset=stream.native_data_offset;
    result.native_payload_bytes=serialized.size()-stream.native_data_offset;
    return result;
}
}
