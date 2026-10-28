#include "ue3_properties.hpp"
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace dotm {
namespace {
std::uint32_t read32(std::span<const std::uint8_t> b, std::size_t off) {
    if (off > b.size() || b.size() - off < 4)
        throw std::runtime_error("Truncated DOTM UObject property stream");
    return (std::uint32_t(b[off]) << 24) | (std::uint32_t(b[off+1]) << 16) |
           (std::uint32_t(b[off+2]) << 8) | std::uint32_t(b[off+3]);
}
std::int32_t signed32(std::span<const std::uint8_t> b, std::size_t off) {
    return std::bit_cast<std::int32_t>(read32(b,off));
}
std::string name_at(const Ue3Package& pkg, std::span<const std::uint8_t> b,
                    std::size_t off) {
    auto idx = signed32(b,off);
    if (idx < 0 || std::size_t(idx) >= pkg.names().size())
        throw std::runtime_error("Invalid DOTM UObject property FName index");
    return pkg.names()[std::size_t(idx)];
}
void check_span(std::span<const std::uint8_t> b, std::size_t off, std::size_t count) {
    if (off > b.size() || count > b.size()-off)
        throw std::runtime_error("DOTM UObject property payload out of bounds");
}
}
Ue3PropertyStream parse_ue3_tagged_properties(const Ue3Package& pkg, std::size_t index) {
    const auto b = pkg.serialized_object(index);
    if (b.size() < 12)
        throw std::runtime_error("No complete UObject net-index and property terminator");
    Ue3PropertyStream result;
    result.net_index = signed32(b,0);
    std::size_t cursor=4;
    // Do not parse arbitrary class/script native data as a tagged-property stream.
    // Caller should restrict this to classes known to have UObject properties.
    for(std::size_t count=0;count<65536;++count) {
        check_span(b,cursor,8);
        auto name=name_at(pkg,b,cursor);
        auto number=signed32(b,cursor+4);
        cursor+=8;
        if(name=="None") {
            result.native_data_offset=cursor;
            return result;
        }
        check_span(b,cursor,16);
        Ue3TaggedProperty prop;
        prop.name=std::move(name);
        prop.name_number=number;
        prop.type=name_at(pkg,b,cursor);
        prop.type_number=signed32(b,cursor+4);
        auto size=signed32(b,cursor+8);
        prop.array_index=signed32(b,cursor+12);
        cursor+=16;
        if(size<0 || prop.array_index<0)
            throw std::runtime_error("Negative DOTM UE3 property length or array index");
        prop.serialized_size=std::uint32_t(size);
        if(prop.type=="StructProperty") {
            check_span(b,cursor,8);
            prop.struct_name=name_at(pkg,b,cursor);
            cursor+=8;
        }else if(prop.type=="BoolProperty") {
            const auto encoded=read32(b,cursor);
            if(encoded>1)
                throw std::runtime_error("Unexpected non-boolean DOTM BoolProperty flag");
            prop.bool_value=(encoded!=0);
            cursor+=4;
        }
        // DOTM ByteProperty values can be serialized FNames (8 bytes);
        // there is NO separate enum-name FName in the property header.
        check_span(b,cursor,prop.serialized_size);
        prop.value_offset=cursor;
        cursor+=prop.serialized_size;
        result.properties.push_back(std::move(prop));
    }
    throw std::runtime_error("Unreasonable DOTM UObject property count");
}
std::span<const std::uint8_t> ue3_property_payload(
    const Ue3Package& pkg,std::size_t export_index,const Ue3TaggedProperty& tag) {
    auto bytes=pkg.serialized_object(export_index);
    check_span(bytes,tag.value_offset,tag.serialized_size);
    return bytes.subspan(tag.value_offset,tag.serialized_size);
}
std::int32_t ue3_int_property(std::span<const std::uint8_t> bytes) {
    if(bytes.size()!=4)throw std::invalid_argument("Expected 4-byte UE3 integer");
    return signed32(bytes,0);
}
float ue3_float_property(std::span<const std::uint8_t> bytes) {
    if(bytes.size()!=4)throw std::invalid_argument("Expected 4-byte UE3 float");
    return std::bit_cast<float>(read32(bytes,0));
}
}
