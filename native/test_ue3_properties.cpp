#include "ue3_properties.hpp"
#include "ue3_texture.hpp"
#include "ue3_mip.hpp"
#include <lzokay.hpp>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
namespace {
void set32(std::vector<std::uint8_t>&b,std::size_t pos,std::uint32_t v){
 if(pos+4>b.size())b.resize(pos+4);
 b[pos]=v>>24;b[pos+1]=v>>16;b[pos+2]=v>>8;b[pos+3]=v;
}
void add32(std::vector<std::uint8_t>&b,std::uint32_t v){set32(b,b.size(),v);}
void add_name(std::vector<std::uint8_t>&b,const std::string&n) {
 add32(b,std::uint32_t(n.size()+1));b.insert(b.end(),n.begin(),n.end());b.push_back(0);
 b.insert(b.end(),8,0);
}
void add_fname(std::vector<std::uint8_t>&b,std::uint32_t idx,std::uint32_t nr=0){
 add32(b,idx);add32(b,nr);
}
void add_tag(std::vector<std::uint8_t>&b,std::uint32_t name,std::uint32_t type,
             std::uint32_t len,std::uint32_t idx=0) {
 add_fname(b,name);add_fname(b,type);add32(b,len);add32(b,idx);
}
void check(bool b,const char*msg){if(!b)throw std::runtime_error(msg);}
std::vector<std::uint8_t> example(bool corrupt=false,bool with_mips=false,bool compressed=false) {
 // These are entirely synthetic, never extracted from a commercial game.
 std::vector<std::uint8_t> b(113);
 set32(b,0,0x9E2A83C1U);set32(b,4,0x00AE0219U);
 set32(b,8,0x01020304U);set32(b,16,5);
 for(int i=0;i<4;++i)b[20+i]="None"[i];
 set32(b,25,0);set32(b,29,22);set32(b,33,113);
 const std::vector<std::string> names={"None","SizeX","IntProperty","bEnabled",
 "BoolProperty","Location","StructProperty","Vector","Format","ByteProperty",
 "Value","FloatProperty","SizeY","OriginalSizeX","OriginalSizeY",
 "MipTailBaseIdx","Engine","Texture2D","Class","Core","Package","PF_DXT5"};
 for(const auto&name:names)add_name(b,name);
 const auto import_offset=std::uint32_t(b.size());
 set32(b,45,2);set32(b,49,import_offset);
 for(std::uint32_t v:{19u,0u,20u,0u,0u,16u,0u})add32(b,v);
 for(std::uint32_t v:{19u,0u,18u,0u,0xFFFFFFFFu,17u,0u})add32(b,v);
 const auto exports=std::uint32_t(b.size());
 set32(b,37,1);set32(b,41,exports);
 for(std::uint32_t v:{0xFFFFFFFEu,0u,0u,10u,0u,0u,0u,0u,0u,0u,0u})add32(b,v);
 b.push_back(0); // DOTM export extension flag.
 const auto depends=std::uint32_t(b.size());set32(b,53,depends);
 const auto obj=std::uint32_t(b.size());
 add32(b,42); // UObject net index.
 add_tag(b,1,2,4); add32(b,512); // SizeX:IntProperty
 add_tag(b,12,2,4); add32(b,256); // SizeY
 add_tag(b,13,2,4); add32(b,512); // OriginalSizeX
 add_tag(b,14,2,4); add32(b,256); // OriginalSizeY
 add_tag(b,15,2,4); add32(b,2);  // MipTailBaseIdx
 add_tag(b,3,4,0); add32(b,1);   // bEnabled:BoolProperty (tag itself holds uint32)
 add_tag(b,5,6,12);add_fname(b,7); // Location:StructProperty<Vector>
 add32(b,0x3f800000u);add32(b,0x40000000u);add32(b,0x40400000u);
 add_tag(b,8,9,8);add_fname(b,21); // Format:ByteProperty FName PF_DXT5
 add_fname(b,0);                 // None terminal
 if(!with_mips) {
  add32(b,0x12345678u);          // Native class-specific data after properties
 } else {
  add32(b,3); // Three synthetic mips.
  std::vector<std::uint8_t> source;
  std::uint32_t rawsize=8;
  if(compressed){
   rawsize=32;
   std::vector<std::uint8_t> raw(32,0x5a);
   std::vector<std::uint8_t> packed(lzokay::compress_worst_size(raw.size()));
   std::size_t packed_len=0;
   if(lzokay::compress(raw.data(),raw.size(),packed.data(),packed.size(),packed_len)!=lzokay::EResult::Success)
    throw std::runtime_error("Synthetic LZO texture encoding failed");
   packed.resize(packed_len);
   add32(source,0x9E2A83C1u);add32(source,131072);
   add32(source,std::uint32_t(packed_len));add32(source,rawsize);
   add32(source,std::uint32_t(packed_len));add32(source,rawsize);
   source.insert(source.end(),packed.begin(),packed.end());
  }else source={1,2,3,4,5,6,7,8};
  add32(b,compressed?0x10u:0u);add32(b,rawsize);
  add32(b,std::uint32_t(source.size()));
  const auto ptr=b.size();add32(b,0); // FByteBulkData offsetInFile.
  add32(b,0xDEADBEEFu);             // Opaque per-mip field.
  set32(b,ptr,std::uint32_t(b.size()));
  b.insert(b.end(),source.begin(),source.end());
  add32(b,32);add32(b,32);
  add32(b,0x11);add32(b,16384);add32(b,1234);add32(b,0x800000);
  add32(b,0xAABBCCDDu);add32(b,16);add32(b,16);
  add32(b,0x21);add32(b,0);add32(b,0xFFFFFFFFu);add32(b,0xFFFFFFFFu);
  add32(b,0x55);add32(b,8);add32(b,8);
 }
 set32(b,exports+32,std::uint32_t(b.size())-obj);
 set32(b,exports+36,obj);
 set32(b,12,std::uint32_t(b.size()));
 if(corrupt){
   // Corrupt first tag's name index; parser must stop with error.
   set32(b,obj+4,0xffffffffu);
 }
 return b;
}
}
int main(){
 try {
  const auto pkg=dotm::Ue3Package::parse(example());
  const auto p=dotm::parse_ue3_tagged_properties(pkg,0);
  check(p.net_index==42,"Net index");
  check(p.properties.size()==8,"Property tag count");
  check(p.properties[0].name=="SizeX"&&p.properties[0].type=="IntProperty","Int tag");
  check(dotm::ue3_int_property(dotm::ue3_property_payload(pkg,0,p.properties[0]))==512,"Int value");
  check(p.properties[5].name=="bEnabled"&&p.properties[5].serialized_size==0 &&
        p.properties[5].bool_value==true,"Inline 32-bit bool");
  check(p.properties[6].struct_name=="Vector"&&p.properties[6].serialized_size==12,"Struct FName");
  auto vector=dotm::ue3_property_payload(pkg,0,p.properties[6]);
  check(dotm::ue3_float_property(vector.subspan(0,4))==1.0f &&
        dotm::ue3_float_property(vector.subspan(4,4))==2.0f &&
        dotm::ue3_float_property(vector.subspan(8,4))==3.0f,"Big-endian vector");
  check(p.properties[7].type=="ByteProperty"&&
        dotm::ue3_property_payload(pkg,0,p.properties[7]).size()==8,"ByteProperty enum");
  const auto tx=dotm::inspect_ue3_texture2d(pkg,0);
  check(tx.width==512 && tx.height==256 && tx.original_width==512 &&
        tx.original_height==256,"Texture2D dimensions");
  check(tx.format=="PF_DXT5" && tx.mip_tail_base_index==2,"Texture2D format and mip");
  check(tx.native_payload_bytes==4,"Texture2D native serialized data length");
  const auto native=pkg.serialized_object(0);
  check(native.size()-p.native_data_offset==4,"Native trailing object payload");
  for(const bool compressed:{false,true}){
   auto fixture=dotm::Ue3Package::parse(example(false,true,compressed));
   const auto mip=dotm::parse_ue3_texture_mips(fixture,0);
   check(mip.size()==3,"UE3 mipmap count");
   check(mip[0].width==32&&mip[0].height==32,"Embedded mip dimensions");
   check(mip[0].storage==(compressed?dotm::Ue3MipStorage::LzoEmbedded:dotm::Ue3MipStorage::Embedded),"Mip storage flag");
   check(mip[0].opaque_word==0xDEADBEEFu,"Preserve opaque mip word");
   auto bytes=dotm::read_ue3_texture_mip_bytes(fixture,0,mip[0]);
   check(bytes.size()==(compressed?32u:8u),"Mip raw/LZO decoded length");
   check(bytes[0]==(compressed?0x5a:1),"Mip binary payload");
   check(mip[1].storage==dotm::Ue3MipStorage::External &&
         mip[2].storage==dotm::Ue3MipStorage::Absent,"Streaming and absent mips");
   bool external_rejected=false;
   try{(void)dotm::read_ue3_texture_mip_bytes(fixture,0,mip[1]);}
   catch(const std::runtime_error&){external_rejected=true;}
   check(external_rejected,"Do not fabricate external mip data");
  }
  bool threw=false;
  try {
   const auto broken=dotm::Ue3Package::parse(example(true));
   (void)dotm::parse_ue3_tagged_properties(broken,0);
  }catch(const std::runtime_error&){threw=true;}
  check(threw,"Corrupt FName detected");
  std::cout<<"DOTM synthetic UObject properties OK\n";
  return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
