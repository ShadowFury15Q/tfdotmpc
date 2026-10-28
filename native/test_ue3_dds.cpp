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
  std::cout<<"DOTM Xbox360 BC1/BC3 detile and DDS header tests PASS\n";
  return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
