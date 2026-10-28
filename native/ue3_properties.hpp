#pragma once
#include "ue3_package.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dotm {

// The format used by DOTM (UE3 package version 537 / licensee 174):
// - 4-byte UObject net index, followed by named properties
// - FName (name index + number), FName (type index + number)
// - int32 serialized size, int32 array index
// - StructProperty adds a struct FName (8 bytes)
// - BoolProperty stores a 32-bit value IN THE TAG and has zero payload bytes
// - ByteProperty does NOT add another header FName in this build
// - FName "None" terminates tags; native-class payload may follow it.
struct Ue3TaggedProperty {
    std::string name;
    std::string type;
    std::string struct_name;
    std::int32_t name_number = 0;
    std::int32_t type_number = 0;
    std::int32_t array_index = 0;
    std::uint32_t serialized_size = 0;
    std::size_t value_offset = 0; // offset relative to serialized UObject start
    std::optional<bool> bool_value;
};
struct Ue3PropertyStream {
    std::int32_t net_index = 0;
    std::vector<Ue3TaggedProperty> properties;
    std::size_t native_data_offset = 0; // after the terminating None FName
};
Ue3PropertyStream parse_ue3_tagged_properties(const Ue3Package& package, std::size_t export_index);
std::span<const std::uint8_t> ue3_property_payload(
    const Ue3Package& package, std::size_t export_index, const Ue3TaggedProperty& tag);
std::int32_t ue3_int_property(std::span<const std::uint8_t> bytes);
float ue3_float_property(std::span<const std::uint8_t> bytes);
}
