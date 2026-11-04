#include "resident_geometry.hpp"
#include <cassert>
#include <random>
#include <cstdio>
int main(){
 using namespace pocket;std::mt19937 rng(174);unsigned vertices_checked=0,boxes=0;
 for(unsigned n=0;n<20000;++n){
  rigid::Parameters p;p.world={1,0,0,0,1,0,0,0,1,0,0,0};p.view=p.world;
  const float angle=float(rng()%6283)*.001f;p.world[0]=std::cos(angle);p.world[1]=std::sin(angle);p.world[3]=-p.world[1];p.world[4]=p.world[0];
  p.world[9]=float(int(rng()%1000)-500)*.01f;p.world[10]=float(int(rng()%1000)-500)*.01f;p.world[11]=float(int(rng()%1000)-500)*.002f;
  p.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  if(n&1){p.projection[11]=-1;p.projection[15]=0;p.projection[14]=-.1f;}
  p.viewport={240,-136,30000,2048,2048,32767,1808,1912};rigid::Transform t;assert(t.build(p));
  resident::Packet packet;
  for(unsigned j=0;j<3;++j){packet.low[j]=float(int(rng()%1000)-500)*.002f;packet.high[j]=packet.low[j]+float(rng()%1000)*.001f;}
  if(packet.accepts(t)){
   ++boxes;for(unsigned v=0;v<100;++v){float q[3];for(unsigned j=0;j<3;++j)q[j]=packet.low[j]+(packet.high[j]-packet.low[j])*float(rng()%10001)/10000;
    assert(t.accepts(q[0],q[1],q[2]));++vertices_checked;
   }
  }
 }
 assert(boxes>1000);std::printf("PASS: %u accepted boxes / %u interior vertices satisfy reference conservative clipping classifier\n",boxes,vertices_checked);
 // Exact identity, all byte changes and budget eviction; retained packets survive eviction.
 resident::Cache cache;std::vector<unsigned char> mem(1024*1024);const auto read=[&](unsigned a,unsigned n)->const unsigned char*{return a+n<=mem.size()?mem.data()+a:nullptr;};
 for(unsigned i=0;i<8;++i){mem[100+i]=i;mem[200+i*12+6]=i;}
 auto first=cache.get(200,100,8,1,4,false,{1,1,0,0},read);assert(first);
 auto again=cache.get(200,100,8,1,4,false,{1,1,0,0},read);assert(first==again);
 for(unsigned at:{200u,204u,206u,100u}){mem[at]^=1;auto changed=cache.get(200,100,8,1,4,false,{1,1,0,0},read);assert(changed&&changed->id!=again->id);again=changed;}
 assert(first->triangles.size()==18); // a queued shared packet is immutable
 for(unsigned i=0;i<3000;++i){auto q=cache.get(200,100,8,1,4,false,{1,1,float(i),0},read);assert(q);assert(cache.used<=resident::Cache::Budget);}
 assert(cache.entries.size()<=2048&&first->triangles.size()==18);
 assert(!cache.get(200,mem.size()-1,8,1,4,false,{1,1,0,0},read));
 puts("PASS: exact source identity, UV/color/position/index invalidation, bounded eviction, retained ownership, invalid memory");
}
