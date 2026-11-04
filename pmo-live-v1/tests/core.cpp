#include "ge.hpp"
#include <fstream>
#include <cassert>
#include <bit>
#include <cstdio>
#include <cmath>
namespace psprecomp{std::int32_t runtime_thread_uid()noexcept{return 0;}const char*runtime_thread_name()noexcept{return "pmo-live";}std::uint32_t runtime_dispatch_pc()noexcept{return 0;}}
using namespace mhf::psp::gpu;
struct Reference:Backend {
 bool native=false;
 std::vector<float> clipped,unclipped;std::vector<unsigned> source;
 bool supports_native_pmo()const override{return native;}
 void add(std::vector<float>&out,const Vertex&v){for(float f:{v.x,v.y,v.z,v.w,v.u,v.v,v.r,v.g,v.b,v.a,v.sr,v.sg,v.sb,v.fog,v.world[0],v.world[1],v.world[2]})out.push_back(f);}
 void triangle_unclipped(const Ge&,const Vertex&a,const Vertex&b,const Vertex&c)override{for(auto*v:{&a,&b,&c}){add(unclipped,*v);source.push_back(v->source);}}
 void triangle(const Ge&,const Vertex&a,const Vertex&b,const Vertex&c)override{for(auto*v:{&a,&b,&c})add(clipped,*v);}
 void sprite(const Ge&,const Vertex&,const Vertex&)override{}
 void block_transfer(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned)override{}
 void flush()override{}void present(unsigned,unsigned,unsigned)override{}
 void clear(){clipped.clear();unclipped.clear();source.clear();}
};
template<class T>void get(std::ifstream&f,T&v){f.read(reinterpret_cast<char*>(&v),sizeof(v));assert(f.good());}
int main(int argc,char**argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary);unsigned magic,self,tmh,mesh,count;get(f,magic);assert(magic==0x504d4f31);get(f,self);get(f,tmh);get(f,mesh);get(f,count);
 psprecomp::GuestMemory mem;
 for(unsigned i=0;i<count;++i){unsigned a,n;get(f,a);get(f,n);assert(n<1024*1024);auto*p=mem.raw_pointer(a,n);assert(p);f.read(reinterpret_cast<char*>(p),n);assert(f.good());}
 Ge ge(mem);Reference ref;ge.set_backend(&ref);
 auto reg=[&](unsigned r,unsigned v){mem.store32(0x08000000,(r<<24)|(v&0xffffff));mem.store32(0x08000004,0x0c000000);auto done=ge.run(0x08000000,0,{});assert(done.ended);};
 auto flt=[&](unsigned r,float v){reg(r,std::bit_cast<unsigned>(v)>>8);};
 unsigned tests=0,triangles=0;
 const auto header=pocket::pmo::u32(mem.raw_pointer(self,32));auto *h=mem.raw_pointer(header,56);assert(h);const auto meshes=pocket::pmo::u16(h+28);
 for(unsigned pose=0;pose<12;++pose) {
  reg(0x3a,0);float angle=float(pose)*.23f,ca=std::cos(angle),sa=std::sin(angle);
  for(float v:{ca,sa,0.0f,-sa,ca,0.0f,0.0f,0.0f,1.0f,pose<6?0.0f:18.0f,0.0f,float(int(pose)-5)*.4f})flt(0x3b,v);
  reg(0x3e,0);for(float v:std::array<float,16>{1,0,0,0,0,1,0,0,0,0,1,pose&1?-1.0f:0.0f,0,0,0,1})flt(0x3f,v);
  unsigned k=0;for(float v:{240.0f,-136.0f,30000.0f,2048.0f,2048.0f,32767.0f})flt(0x42+k++,v);
  reg(0x4c,1808*16);reg(0x4d,1912*16);reg(0x50,pose&1);flt(0x4a,.125f);flt(0x4b,-.25f);
  for(mesh=0;mesh<meshes;++mesh) {
   mem.store32(0x089cc000,0x08001000);assert(pocket::pmo::produce(mem,self,tmh,mesh));const auto end=mem.load32(0x089cc000);mem.store32(end,0x0c000000);
   ref.native=false;ref.clear();const auto before=ge.stats();auto done=ge.run(0x08001000,0,{});assert(done.ended);
   const auto clipped=ref.clipped,unclipped=ref.unclipped;const auto source=ref.source;std::array<unsigned,256> expected{};for(unsigned i=0;i<256;++i)expected[i]=ge.reg(i);
   const auto commands=ge.stats().commands-before.commands,primitives=ge.stats().primitives-before.primitives,input=ge.stats().vertices-before.vertices;
   ref.native=true;ref.clear();const auto before2=ge.stats();done=ge.run(0x08001000,0,{});assert(done.ended);
   assert(ref.clipped==clipped&&ref.unclipped==unclipped&&ref.source==source);
   for(unsigned i=0;i<256;++i)assert(ge.reg(i)==expected[i]);
   assert(ge.stats().commands-before2.commands==commands&&ge.stats().primitives-before2.primitives==primitives&&ge.stats().vertices-before2.vertices==input);
   ++tests;triangles+=source.size()/3;
  }
 }
 // Stalling inside a native-eligible group must retain CALL/RET stack semantics.
 const auto geom=mem.load32(self+4);const auto group=geom+mem.load32(header+mem.load32(header+36)+4);const auto stall=group+24;
 reg(0x3a,9);flt(0x3b,0);flt(0x3b,0);flt(0x3b,0);
 mem.store32(0x089cc000,0x08001000);assert(pocket::pmo::produce(mem,self,tmh,0));mem.store32(mem.load32(0x089cc000),0x0c000000);
 ref.native=false;ref.clear();auto done=ge.run(0x08001000,0,{});assert(done.ended);const auto expected=ref.clipped;
 ref.native=true;ref.clear();done=ge.run(0x08001000,stall,{});assert(done.stalled&&done.pc==stall);done=ge.run(done.pc,0,{});assert(done.ended&&ref.clipped==expected);
 // Same VRAM, changed color/UV/position: snapshot invalidation must be exact.
 const unsigned va=group+(mem.load32(group+12)&0xffffff);
 for(unsigned offset:{0u,4u,6u}) {
  mem.store16(va+offset,mem.load16(va+offset)^0x1234);
  ref.native=false;ref.clear();ge.run(0x08001000,0,{});const auto wanted=ref.clipped;
  ref.native=true;ref.clear();ge.run(0x08001000,0,{});assert(ref.clipped==wanted);
 }
 ge.geometry_probe_present();
 puts("PASS: source mutation and CALL/RET stall/resume preserve clipped output");
 std::printf("PASS: %u real model mesh/pose comparisons, %u reference triangles; clipped/unclipped attributes, sources, all 256 registers, commands/primitives/vertex counters identical\n",tests,triangles);
}
