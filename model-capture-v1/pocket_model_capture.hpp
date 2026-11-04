#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace pocket_model_capture {
static_assert(std::endian::native==std::endian::little);
static_assert(sizeof(unsigned)==4 && sizeof(float)==4);
#ifndef POCKET_MODEL_CAPTURE_PATH
#define POCKET_MODEL_CAPTURE_PATH "ux0:data/pocketprobe/model-capture.bin"
#endif
inline constexpr unsigned Limit=12u*1024u*1024u,MaxDraws=32,MaxGeometry=2u*1024u*1024u;
enum Tag:unsigned {ModelMeta=1,Object=2,HeaderTables=3,GeometryEntry=4,Materials=5,
    MeshLighting=6,TextureObject=7,TextureHeader=8,TextureFragments=9,GeometryGE=10,
    DrawMeta=100,Registers=101,World=102,View=103,Projection=104,Texgen=105,
    Bones=106,Clut=107,Indices=108,Vertices=109,Commands=110,TextureInfo=111,
    TextureData=112,Finished=255};
inline std::uint32_t load32(const void*p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
inline unsigned load16(const void*p){std::uint16_t v;std::memcpy(&v,p,2);return v;}
inline unsigned hash(const void*p,unsigned n){const auto*b=static_cast<const std::uint8_t*>(p);unsigned h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^b[i])*16777619u;return h;}
// Model thread publishes immutable identity with release/acquire. Only the GE
// worker changes records/file state after publication. No FILE crosses threads.
inline std::atomic<unsigned> phase{0}; // 0 waiting, 1 copying model, 2 ready, 3 done, 4 error
inline unsigned geometry_address=0,geometry_size=0,bytes_written=0,draws=0;
struct TextureKey {unsigned address{},size{},hash{};};
inline std::array<TextureKey,4> textures{};
inline unsigned texture_count=0;
inline unsigned canonical(unsigned a){a&=0x0fffffffu;return (a&0x0f000000u)==0x04000000u?0x04000000u|(a&0x1fffffu):a;}
inline void fail(const char*reason){phase.store(4,std::memory_order_release);std::fprintf(stderr,"[model-capture-v1] failed: %s\n",reason);}
class File {
    FILE*f_=nullptr;bool good_=true;
public:
    explicit File(bool first){
        f_=std::fopen(POCKET_MODEL_CAPTURE_PATH,first?"wb":"ab");
        if(!f_){good_=false;return;}
        if(first){bytes_written=8;const char magic[8]={'P','M','O','C','A','P','1',0};good_=std::fwrite(magic,1,8,f_)==8;}
    }
    File(const File&)=delete;
    ~File(){if(f_)std::fclose(f_);}
    bool room(unsigned n)const{return good_&&n<=Limit&&bytes_written<=Limit-n;}
    bool chunk(unsigned tag,unsigned address,const void*data,unsigned n){
        if(!f_||!room(16u+n)){good_=false;return false;}
        const unsigned h[4]={tag,address,n,hash(data,n)};
        good_=std::fwrite(h,1,16,f_)==16 && (!n||std::fwrite(data,1,n,f_)==n);
        if(good_)bytes_written+=16+n;
        return good_;
    }
    template<class Reader>bool guest(unsigned tag,unsigned a,unsigned n,Reader&&read){
        const auto*p=n?read(a,n):nullptr;
        if(!p)return chunk(tag,a,nullptr,0);
        // Take a bounded copy before file I/O. This is not a stop-the-world
        // snapshot: entry and GE copies are kept separately for comparison.
        std::vector<std::uint8_t> copy(p,p+n);return chunk(tag,a,copy.data(),n);
    }
    bool close(){if(!f_)return false;const bool flushed=std::fflush(f_)==0;const bool closed=std::fclose(f_)==0;f_=nullptr;return good_&&flushed&&closed;}
};
template<class Memory>void model(Memory&mem,unsigned self,unsigned hierarchy,unsigned tmh,unsigned mesh,unsigned ra)noexcept{
    if(hierarchy || phase.load(std::memory_order_acquire)!=0)return;
    try{
        auto read=[&](unsigned a,unsigned n){return mem.raw_pointer(a,n);};
        const auto*o=read(self,32);if(!o)return;
        const unsigned hp=load32(o),ga=load32(o+4),mp=load32(o+8),lp=load32(o+12);
        const auto*h=read(hp,56);if(!h||load32(h)!=0x006f6d70u||load32(h+4)!=0x00302e31u)return;
        const unsigned size=load32(h+8),go=load32(h+52),meshes=load16(h+28),mats=load16(h+30);
        if(meshes<8||meshes>4096||(mesh&255u)>=meshes||mats>4096||go<56||go>512u*1024u||size<=go||size-go>MaxGeometry)return;
        if(!read(hp,go)||!read(ga,size-go))return;
        unsigned waiting=0;if(!phase.compare_exchange_strong(waiting,1,std::memory_order_acq_rel))return;
        File f(true);
        const unsigned meta[]={1,self,hierarchy,tmh,mesh,ra,hp,ga,size-go,size,go,meshes,mats};
        f.chunk(ModelMeta,0,meta,sizeof(meta));
        f.guest(Object,self,32,read);f.guest(HeaderTables,hp,go,read);
        f.guest(GeometryEntry,ga,size-go,read);
        f.guest(Materials,mp,mats*16u,read);f.guest(MeshLighting,lp,meshes*8u,read);
        f.guest(TextureObject,tmh,16,read);
        if(const auto*t=read(tmh,16)){
            const auto tp=load32(t),fp=load32(t+4);f.guest(TextureHeader,tp,16,read);
            const auto*th=read(tp,16);const unsigned count=th?load32(th+8):0;
            if(count&&count<=256)f.guest(TextureFragments,fp,count*32u,read);
        }
        if(!f.close()){fail("model file write");return;}
        geometry_address=ga;geometry_size=size-go;
        phase.store(2,std::memory_order_release);
        std::fprintf(stderr,"[model-capture-v1] model saved: this=%08X header=%08X geometry=%08X bytes=%u; waiting for matching GE commands\n",self,hp,ga,geometry_size);
    }catch(...){fail("model snapshot exception");}
}
template<class Reader,class Texture>void primitive(unsigned pc,unsigned command,unsigned stride,
    const std::uint32_t*regs,const float*world,const float*view,const float*projection,
    const float*texgen,const void*bones,unsigned bone_bytes,const void*clut,unsigned clut_bytes,
    const Texture&ti,Reader&&read)noexcept{
    if(phase.load(std::memory_order_acquire)!=2)return;
    const auto base=canonical(geometry_address),at=canonical(pc);
    if(at<base||at-base>=geometry_size)return;
    try{
        File f(false);
        if(draws==0)f.guest(GeometryGE,geometry_address,geometry_size,read);
        // Stop cleanly before another potentially large record. The previous
        // complete records remain useful even if the configured budget fills.
        if(!f.room(2u*1024u*1024u)){
            const unsigned stop[]={draws,bytes_written,1};f.chunk(Finished,0,stop,sizeof(stop));
            if(!f.close()){fail("budget footer write");return;}
            phase.store(3,std::memory_order_release);std::fprintf(stderr,"[model-capture-v1] saved %u GE draws; byte budget reached\n",draws);return;
        }
        const unsigned count=command&65535u,format=(regs[0x12]>>11)&3u;
        const unsigned width=format==1?1u:format==2?2u:format==3?4u:0u;
        const unsigned va=regs[1],ia=regs[2];unsigned maxindex=count?count-1u:0u;
        bool index_ok=true;
        if(width){const auto*i=read(ia,count*width);index_ok=i!=nullptr;maxindex=0;
            if(i)for(unsigned n=0;n<count;++n){const auto x=width==1?unsigned(i[n]):width==2?load16(i+n*2):load32(i+n*4);if(x>maxindex)maxindex=x;}}
        const std::uint64_t span=(std::uint64_t(maxindex)+1u)*stride;
        const unsigned vertex_bytes=count&&index_ok&&span<=256u*1024u?static_cast<unsigned>(span):0;
        const unsigned meta[]={draws,pc,command,stride,va,ia,width,count,maxindex,vertex_bytes,index_ok?1u:0u};
        f.chunk(DrawMeta,0,meta,sizeof(meta));f.chunk(Registers,0,regs,256*4);
        f.chunk(World,0,world,48);f.chunk(View,0,view,48);f.chunk(Projection,0,projection,64);
        f.chunk(Texgen,0,texgen,48);f.chunk(Bones,0,bones,bone_bytes);f.chunk(Clut,0,clut,clut_bytes);
        f.guest(Indices,ia,count*width,read);f.guest(Vertices,va,vertex_bytes,read);f.guest(Commands,pc,32,read);
        unsigned texture_hash=0;const auto*tex=(regs[0x1e]&1u)&&ti.bytes&&ti.bytes<=1024u*1024u?read(ti.address,ti.bytes):nullptr;
        std::vector<std::uint8_t> texture_copy;
        if(tex){texture_copy.assign(tex,tex+ti.bytes);texture_hash=hash(texture_copy.data(),ti.bytes);}
        const unsigned info[]={ti.address,ti.stride,ti.format,ti.width,ti.height,ti.bytes,ti.swizzled?1u:0u,texture_hash};
        f.chunk(TextureInfo,0,info,sizeof(info));
        bool seen=false;for(unsigned i=0;i<texture_count;++i)if(textures[i].address==ti.address&&textures[i].size==ti.bytes&&textures[i].hash==texture_hash)seen=true;
        if(tex&&!seen&&texture_count<textures.size()){
            f.chunk(TextureData,ti.address,texture_copy.data(),ti.bytes);textures[texture_count++]={ti.address,ti.bytes,texture_hash};
        }
        ++draws;
        if(draws==MaxDraws){const unsigned stop[]={draws,bytes_written,0};f.chunk(Finished,0,stop,sizeof(stop));}
        if(!f.close()){fail("GE record write");return;}
        if(draws==MaxDraws){phase.store(3,std::memory_order_release);std::fprintf(stderr,"[model-capture-v1] complete: %u GE draws, %u bytes -> %s\n",draws,bytes_written,POCKET_MODEL_CAPTURE_PATH);}
    }catch(...){fail("GE snapshot exception");}
}
} // namespace pocket_model_capture
