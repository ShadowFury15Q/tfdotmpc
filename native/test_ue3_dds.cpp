#include "ue3_dds.hpp"
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void check(bool ok,const char*text){if(!ok)throw std::runtime_error(text);}
std::uint32_t readle(const std::vector<std::uint8_t>&v,std::size_t p){
 return std::uint32_t(v.at(p))|(std::uint32_t(v.at(p+1))<<8)|
        (std::uint32_t(v.at(p+2))<<16)|(std::uint32_t(v.at(p+3))<<24);
}
}
int main(){
 try {
  // A 32x32 BC1 texture still occupies a minimum 128x128 Xbox 360 GPU tile.
  std::vector<std::uint8_t> tiled(8192,0);
  for(std::size_t n=0;n<8;++n)tiled[n]=std::uint8_t(n);
  const auto dds=dotm::convert_xbox_tiled_bc_to_dds(32,32,false,tiled);
  check(dds.size()==128+8*8*8,"BC1 DDS size");
  check(dds[0]=='D'&&dds[1]=='D'&&dds[2]=='S'&&dds[3]==' ',"DDS signature");
  check(dds[84]=='D'&&dds[85]=='X'&&dds[86]=='T'&&dds[87]=='1',"DXT1 FourCC");
  check(readle(dds,12)==32&&readle(dds,16)==32,"DDS dimensions");
  check(dds[128]==1&&dds[129]==0&&dds[130]==3&&dds[131]==2,"Xenos 16-bit word swapping");
  std::vector<std::uint8_t> tiled5(16384,0);
  tiled5[0]=0xa1;tiled5[1]=0xb2;
  const auto dds5=dotm::convert_xbox_tiled_bc_to_dds(32,32,true,tiled5);
  check(dds5.size()==128+8*8*16&&dds5[87]=='5',"DXT5 FourCC/size");
  check(dds5[128]==0xb2&&dds5[129]==0xa1,"DXT5 word swap");
  bool threw=false;
  try{(void)dotm::convert_xbox_tiled_bc_to_dds(128,128,false,{tiled.data(),16});}
  catch(const std::runtime_error&){threw=true;}
  check(threw,"Reject truncated Xbox tile");
  // Stage 41: DDS mip chain headers and contiguous BC1/BC3 payload layout.
  const std::vector<dotm::XboxTiledBcMip> bc1_chain{
      {32,32,tiled},{16,16,tiled5}};
  const auto bc1_mips=dotm::convert_xbox_tiled_bc_mips_to_dds(bc1_chain,false);
  check(bc1_mips.size()==128+512+128,"BC1 mip chain byte count");
  check(readle(bc1_mips,8)==0xa1007u&&readle(bc1_mips,28)==2,
        "DDSD_MIPMAPCOUNT and mip count");
  check(readle(bc1_mips,20)==512&&readle(bc1_mips,108)==0x401008u,
        "DDS top mip size and mipmap caps");
  check(bc1_mips[128]==1&&bc1_mips[128+512]==0xb2&&
        bc1_mips[128+513]==0xa1,"BC1 second mip offset and byte swap");
  const std::vector<dotm::XboxTiledBcMip> bc3_chain{
      {32,32,tiled5},{16,16,tiled}};
  const auto bc3_mips=dotm::convert_xbox_tiled_bc_mips_to_dds(bc3_chain,true);
  check(bc3_mips.size()==128+1024+256&&bc3_mips[87]=='5',
        "BC3 mip chain size and FourCC");
  check(bc3_mips[128+1024]==1&&bc3_mips[128+1025]==0,
        "BC3 second mip offset and byte swap");
  const std::vector<dotm::XboxTiledBcMip> one{{32,32,tiled}};
  check(dotm::convert_xbox_tiled_bc_mips_to_dds(one,false)==dds,
        "Single-mip API must remain byte-identical");
  const std::vector<dotm::XboxTiledBcMip> odd{
      {7,5,tiled},{3,2,tiled5},{1,1,tiled}};
  check(dotm::convert_xbox_tiled_bc_mips_to_dds(odd,false).size()==128+32+8+8,
        "Non-power-of-two mip dimensions");
  threw=false;
  try{(void)dotm::convert_xbox_tiled_bc_mips_to_dds(
      std::span<const dotm::XboxTiledBcMip>{},false);}
  catch(const std::invalid_argument&){threw=true;}
  check(threw,"Reject empty mip chain");
  const std::vector<dotm::XboxTiledBcMip> gap{{32,32,tiled},{7,7,tiled5}};
  threw=false;
  try{(void)dotm::convert_xbox_tiled_bc_mips_to_dds(gap,false);}
  catch(const std::invalid_argument&){threw=true;}
  check(threw,"Reject noncontiguous mip dimensions");
  const std::vector<dotm::XboxTiledBcMip> repeated_tail{
      {1,1,tiled},{1,1,tiled5}};
  threw=false;
  try{(void)dotm::convert_xbox_tiled_bc_mips_to_dds(repeated_tail,false);}
  catch(const std::invalid_argument&){threw=true;}
  check(threw,"Reject duplicate 1x1 mip");
  const std::vector<dotm::XboxTiledBcMip> truncated_chain{
      {32,32,tiled},{16,16,std::span<const std::uint8_t>(tiled.data(),1)}};
  threw=false;
  try{(void)dotm::convert_xbox_tiled_bc_mips_to_dds(truncated_chain,false);}
  catch(const std::runtime_error&){threw=true;}
  check(threw,"Reject truncated second mip");
  std::cout<<"DOTM Xbox360 BC1/BC3 detile and DDS mip chain/header tests PASS\n";
  return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
