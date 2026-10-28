#include "ue3_package.hpp"
#include <lzokay.hpp>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void put32(std::vector<std::uint8_t>& b,std::size_t i,std::uint32_t x) {
 if(i+4>b.size()) b.resize(i+4);
 b[i]=std::uint8_t(x>>24);b[i+1]=std::uint8_t(x>>16);b[i+2]=std::uint8_t(x>>8);b[i+3]=std::uint8_t(x);
}
void append32(std::vector<std::uint8_t>&b,std::uint32_t x) {put32(b,b.size(),x);}
void append_name(std::vector<std::uint8_t>&b,const std::string&n) {
 append32(b,std::uint32_t(n.size()+1));
 b.insert(b.end(),n.begin(),n.end());b.push_back(0);
 b.insert(b.end(),8,0);
}
void require(bool ok,const char*msg){if(!ok)throw std::runtime_error(msg);}
std::vector<std::uint8_t> mock_package(){
 std::vector<std::uint8_t> b(113,0);
 put32(b,0,0x9E2A83C1U);
 put32(b,4,0x00AE0219U);
 put32(b,8,0x4DC90454U);
 put32(b,16,5);
 const std::string folder="None";
 std::copy(folder.begin(),folder.end(),b.begin()+20);
 put32(b,25,0x00080009U);
 put32(b,29,3);
 put32(b,33,113);
 put32(b,37,0);
 append_name(b,"Core");append_name(b,"Package");append_name(b,"Engine");
 put32(b,41,std::uint32_t(b.size()));
 put32(b,45,1);
 put32(b,49,std::uint32_t(b.size()));
 // Import {class package=Core, class name=Package, outer=0, object name=Engine}.
 for(auto v:{0u,0u,1u,0u,0u,2u,0u})append32(b,v);
 put32(b,53,std::uint32_t(b.size()));
 put32(b,12,std::uint32_t(b.size()));
 return b;
}
std::vector<std::uint8_t> compressed(const std::vector<std::uint8_t>&v){
 const auto payload_length=v.size()-113;
 std::vector<std::uint8_t> payload(payload_length+payload_length/16+128,0);
 std::size_t comp_length=0;
 auto result=lzokay::compress(v.data()+113,payload_length,payload.data(),payload.size(),comp_length);
 require(result==lzokay::EResult::Success,"Synthetic LZO compression");
 payload.resize(comp_length);
 const std::uint32_t disk_chunk=0x6D+16;
 std::vector<std::uint8_t> b(v.begin(),v.begin()+0x6D);
 put32(b,0x65,2);
 put32(b,0x69,1);
 append32(b,113);
 append32(b,std::uint32_t(payload_length));
 append32(b,disk_chunk);
 append32(b,std::uint32_t(16+8+comp_length));
 append32(b,0x9E2A83C1U);
 append32(b,131072);
 append32(b,std::uint32_t(comp_length));
 append32(b,std::uint32_t(payload_length));
 append32(b,std::uint32_t(comp_length));
 append32(b,std::uint32_t(payload_length));
 b.insert(b.end(),payload.begin(),payload.end());
 return b;
}
}
int main(){
 try{
  auto plain=mock_package();
  auto p=dotm::Ue3Package::parse(plain);
  require(p.names().size()==3 && p.names()[2]=="Engine","UE3 names");
  require(p.imports().size()==1 && p.imports()[0].object_name=="Engine","UE3 imports");
  require(p.imported_package_names().size()==1 && p.imported_package_names()[0]=="Engine","UE3 root imports");
  require(p.info().version==537 && p.info().licensee_version==174,"DOTM version");
  require(p.info().compression_flags==0,"UE3 uncompressed");
  auto packed=compressed(plain);
  auto p2=dotm::Ue3Package::parse(packed);
  require(p2.names()==p.names(),"LZO decode names");
  require(p2.imported_package_names()==p.imported_package_names(),"LZO decode imports");
  require(p2.info().compression_flags==2 && p2.info().decompressed_blocks==1,"LZO block bookkeeping");
  auto bad=plain;bad[0]=0;
  bool threw=false;try{(void)dotm::Ue3Package::parse(bad);}catch(const std::runtime_error&){threw=true;}
  require(threw,"Corrupt UE3 magic rejected");
  std::cout<<"DOTM UE3 big-endian, names, import and LZO test PASS\n";
  return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
