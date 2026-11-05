#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace pocket::rigid {
using Matrix=std::array<float,16>;
struct Parameters {
    std::array<float,12> world{},view{};
    Matrix projection{};
    std::array<float,8> viewport{};
};
inline Matrix expand(const std::array<float,12>&m){
    return {m[0],m[1],m[2],0,m[3],m[4],m[5],0,m[6],m[7],m[8],0,m[9],m[10],m[11],1};
}
inline Matrix multiply(const Matrix&a,const Matrix&b){
    Matrix c{};
    for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)
        c[j*4+i]=a[i]*b[j*4]+a[4+i]*b[j*4+1]+a[8+i]*b[j*4+2]+a[12+i]*b[j*4+3];
    return c;
}
inline Matrix absolute(Matrix a){for(auto &x:a)x=std::fabs(x);return a;}
struct Transform {
    Matrix clip{},bound{},gpu{};
    std::array<float,8> viewport{};
    bool build(const Parameters&p){
        for(float x:p.world)if(!std::isfinite(x))return false;
        for(float x:p.view)if(!std::isfinite(x))return false;
        for(float x:p.projection)if(!std::isfinite(x))return false;
        for(float x:p.viewport)if(!std::isfinite(x))return false;
        const auto w=expand(p.world),v=expand(p.view);
        clip=multiply(p.projection,multiply(v,w));
        bound=multiply(absolute(p.projection),multiply(absolute(v),absolute(w)));
        viewport=p.viewport;
        Matrix screen{};
        screen[0]=p.viewport[0]*(2.0f/512.0f);
        screen[5]=-p.viewport[1]*(2.0f/512.0f);
        screen[10]=p.viewport[2]*(2.0f/65535.0f);
        screen[12]=(p.viewport[3]-p.viewport[6])*(2.0f/512.0f)-1.0f;
        screen[13]=1.0f-(p.viewport[4]-p.viewport[7])*(2.0f/512.0f);
        screen[14]=p.viewport[5]*(2.0f/65535.0f)-1.0f;
        screen[15]=1;
        gpu=multiply(screen,clip);
        for(float x:clip)if(!std::isfinite(x))return false;
        for(float x:bound)if(!std::isfinite(x))return false;
        for(float x:gpu)if(!std::isfinite(x))return false;
        return true;
    }
    // POCKET_RIGID_CULL_V9: "outside" means certainly rejected by the
    // existing PSP GE triangle pipeline, not merely outside the Vita display.
    enum class Visibility { Safe, OutsideNear, OutsideGuard, Boundary };
    // POCKET_GPU_SKIN_V13D: depth=false for the skin path, whose shader clamps depth exactly as the CPU path does.
    Visibility classify(float x,float y,float z,bool depth=true)const{
        constexpr float eps=128.0f*std::numeric_limits<float>::epsilon();
        const float p[4]={x,y,z,1};float c[4]{},e[4]{};
        for(unsigned i=0;i<4;++i){
            c[i]=clip[i]*p[0]+clip[4+i]*p[1]+clip[8+i]*p[2]+clip[12+i];
            e[i]=eps*(bound[i]*std::fabs(x)+bound[4+i]*std::fabs(y)+bound[8+i]*std::fabs(z)+bound[12+i])+1e-7f;
            if(!std::isfinite(c[i])||!std::isfinite(e[i]))return Visibility::Boundary;
        }
        if(c[3]<-e[3]||c[2]+c[3]<-(e[2]+e[3]))return Visibility::OutsideNear;
        if(c[3]<=e[3]||c[2]+c[3]<=e[2]+e[3])return Visibility::Boundary;
        const auto coordinate=[&](unsigned row,float scale,float center,float limit){
            const float h=c[row]*scale+c[3]*center;
            const float err=e[row]*std::fabs(scale)+e[3]*std::fabs(center)+eps*(std::fabs(c[row]*scale)+std::fabs(c[3]*center));
            const float hi=limit*c[3]-h;
            const float hi_error=limit*e[3]+err+eps*(std::fabs(limit*c[3])+std::fabs(h));
            if(h < -err || hi < -hi_error)return -1;
            if(h>err && hi>hi_error)return 1;
            return 0;
        };
        // Guard band uses coordinates BEFORE screen offset subtraction.
        const int gx=coordinate(0,viewport[0],viewport[3],4096.0f);
        const int gy=coordinate(1,viewport[1],viewport[4],4096.0f);
        if(gx<0||gy<0)return Visibility::OutsideGuard;
        // Depth outside this range is CLAMPED by the reference backend. It
        // must use fallback, never be treated as invisible by this prototype.
        if(gx!=1||gy!=1||(depth&&coordinate(2,viewport[2],viewport[5],65535.0f)!=1))
            return Visibility::Boundary;
        return Visibility::Safe;
    }
    bool accepts(float x,float y,float z)const{return classify(x,y,z)==Visibility::Safe;}

};
struct Input {float position[4],color[4],uv[2];};
inline Input decode_111a(const std::uint8_t*p,float us,float vs,float uo,float vo){
    std::uint16_t u,v,color;std::int16_t x,y,z;
    std::memcpy(&u,p,2);std::memcpy(&v,p+2,2);std::memcpy(&color,p+4,2);
    std::memcpy(&x,p+6,2);std::memcpy(&y,p+8,2);std::memcpy(&z,p+10,2);
    Input out{};
    out.position[0]=float(x)*(1.0f/32768);out.position[1]=float(y)*(1.0f/32768);out.position[2]=float(z)*(1.0f/32768);out.position[3]=1;
    for(unsigned c=0;c<4;++c){const unsigned n=(color>>(c*4))&15;out.color[c]=float(n*17)*(1.0f/255);}
    out.uv[0]=float(u)*(1.0f/32768)*us+uo;out.uv[1]=float(v)*(1.0f/32768)*vs+vo;
    return out;
}
}
