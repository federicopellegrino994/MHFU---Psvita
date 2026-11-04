#include "pocket_pmo_bridge.hpp"
#include <thread>
#include <cassert>
#include <atomic>
#include <cstdio>
int main(){
 using namespace pocket::pmo;
 Registry local;std::atomic<bool> done=false;
 std::thread producer([&]{for(unsigned i=0;i<50000;++i)local.add({0x04100000+(i%79)*4096u,(i%100+1)*4u,i,i+7});done=true;});
 unsigned reads=0;
 while(!done){for(unsigned i=0;i<79;++i){Region r;if(local.get(0x04100000+i*4096,r)){assert(r.self==r.header+7);++reads;}}}
 producer.join();assert(reads);std::printf("PASS: metadata publication across CPU/GE threads, 50000 writes, %u coherent reads\n",reads);
 GroupCache cache;cache.used=GroupCache::Budget;
 Region r{0x04100000,100,1,1};unsigned source_reads=0;
 auto read=[&](unsigned,unsigned)->const std::uint8_t*{++source_reads;return nullptr;};
 for(unsigned i=0;i<10000;++i){r.start+=4;assert(!cache.get(r,read));}
 assert(cache.counts.builds==0&&source_reads==0&&cache.entries.empty());
 puts("PASS: full cache denies new groups before reads/allocations; no eviction/rebuild loop");
 GroupCache failed;
 auto fail=[](unsigned,unsigned)->const std::uint8_t*{throw std::bad_alloc();};
 assert(!failed.get(r,fail)&&failed.allocation_blocked&&failed.entries.empty()&&failed.used==0);
 assert(!failed.get(r,fail));
 puts("PASS: allocation failure disables admission and preserves the legacy fallback");
 // Different instances sharing the same asset header/range have one identity.
 Region a{0x04100000,64,0x09000000,1},b=a;b.self=2;assert(a==b);
 puts("PASS: shared model instances do not invalidate geometry solely by object address");
}
