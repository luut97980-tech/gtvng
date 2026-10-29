#include "game_backend.hpp"
#include "stealth.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace vt::ios {
namespace {
using Obj=void*;using Klass=void*;using Field=void*;using Method=const void*;
struct Vec3 {float x=0,y=0,z=0;};
struct Quat {float x=0,y=0,z=0,w=1;};
struct Bounds {Vec3 center{},extents{};};
struct Rect {float x=0,y=0,w=0,h=0;};

bool valid(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<100000.f&&std::fabs(p.y)<100000.f&&std::fabs(p.z)<100000.f;}
bool validBounds(const Bounds&b){
    return valid(b.center)&&valid(b.extents)&&b.extents.x>=0.f&&b.extents.y>=0.f&&b.extents.z>=0.f&&
        b.extents.x<=100000.f&&b.extents.y<=100000.f&&b.extents.z<=100000.f;
}
float distXZ(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.z-b.z);}
float dist3D(Vec3 a,Vec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}

bool safeReadMemory(const void*src,void*dst,std::size_t size){
    if(!src||!dst||size==0||size>0x1000)return false;
    auto addr=reinterpret_cast<std::uintptr_t>(src);
    if(addr<0x10000)return false;
#if defined(__APPLE__)
    vm_size_t outSize=0;
    if(vm_read_overwrite(mach_task_self(),static_cast<vm_address_t>(addr),static_cast<vm_size_t>(size),
        reinterpret_cast<vm_address_t>(dst),&outSize)!=KERN_SUCCESS||outSize!=size)return false;
    return true;
#else
    std::memcpy(dst,src,size);
    return true;
#endif
}

template<class T>
bool safeReadAt(const void*base,std::size_t offset,T&out){
    if(!base)return false;
    auto addr=reinterpret_cast<std::uintptr_t>(base);
    if(addr<0x10000)return false;
    return safeReadMemory(reinterpret_cast<const char*>(base)+offset,&out,sizeof(T));
}

template<class T>
bool safeWriteAt(void*base,std::size_t offset,const T&val){
    if(!base)return false;
    T probe{};
    if(!safeReadAt(base,offset,probe))return false;
    std::memcpy(reinterpret_cast<char*>(base)+offset,&val,sizeof(T));
    return true;
}

bool isValidObjectPointer(Obj o){
    if(!o)return false;
    auto addr=reinterpret_cast<std::uintptr_t>(o);
    if(addr<0x10000||(addr&(sizeof(void*)-1))!=0)return false;
    void* klassPtr=nullptr;
    if(!safeReadAt(o,0,klassPtr)||!klassPtr)return false;
    auto kaddr=reinterpret_cast<std::uintptr_t>(klassPtr);
    if(kaddr<0x10000||(kaddr&(sizeof(void*)-1))!=0)return false;
    void* probe=nullptr;
    if(!safeReadAt(klassPtr,0,probe))return false;
    return true;
}

bool iequals(const char*a,const char*b){
    if(!a||!b)return false;
    while(*a&&*b){
        if(std::tolower(static_cast<unsigned char>(*a))!=std::tolower(static_cast<unsigned char>(*b)))return false;
        ++a;++b;
    }
    return *a==*b;
}

struct API {
    void*(*domain_get)()=nullptr;
    void**(*domain_get_assemblies)(void*,std::size_t*)=nullptr;
    void*(*assembly_get_image)(void*)=nullptr;
    const char*(*image_get_name)(void*)=nullptr;
    Klass(*class_from_name)(void*,const char*,const char*)=nullptr;
    Klass(*class_get_parent)(Klass)=nullptr;
    const char*(*class_get_name)(Klass)=nullptr;
    const char*(*class_get_namespace)(Klass)=nullptr;
    Method(*class_get_method_from_name)(Klass,const char*,int)=nullptr;
    Field(*class_get_field_from_name)(Klass,const char*)=nullptr;
    const void*(*field_get_type)(Field)=nullptr;
    int(*type_get_type)(const void*)=nullptr;
    void(*field_get_value)(Obj,Field,void*)=nullptr;
    void(*field_static_get_value)(Field,void*)=nullptr;
    Obj(*runtime_invoke)(Method,Obj,void**,Obj*)=nullptr;
    Klass(*object_get_class)(Obj)=nullptr;
    void*(*object_unbox)(Obj)=nullptr;
    std::uintptr_t(*gchandle_new)(Obj,bool)=nullptr;
    Obj(*gchandle_get_target)(std::uintptr_t)=nullptr;
    void(*gchandle_free)(std::uintptr_t)=nullptr;
    Obj(*array_new)(Klass,std::size_t)=nullptr;
    std::size_t(*array_length)(Obj)=nullptr;
    std::uint32_t(*array_object_header_size)()=nullptr;
    void*(*thread_current)()=nullptr;
    void*(*thread_attach)(void*)=nullptr;
    void(*thread_detach)(void*)=nullptr;
    // Optional IL2CPP helpers (resolved when present in UnityFramework)
    void(*field_set_value)(Obj,Field,void*)=nullptr;
    std::size_t(*field_get_offset)(Field)=nullptr;
    int(*field_get_flags)(Field)=nullptr;
    Field(*class_get_fields)(Klass,void**)=nullptr;
    const char*(*field_get_name)(Field)=nullptr;
    const void*(*class_get_type)(Klass)=nullptr;
    Obj(*type_get_object)(const void*)=nullptr;
    Method(*class_get_methods)(Klass,void**)=nullptr;
    const char*(*method_get_name)(Method)=nullptr;
    std::uint32_t(*method_get_param_count)(Method)=nullptr;
    const void*(*method_get_param)(Method,std::uint32_t)=nullptr;
    bool(*method_is_generic)(Method)=nullptr;
    bool hasNativeLayout=false;

    explicit API(const GameBackend::Resolver& r){
#define LOAD(n) n=reinterpret_cast<decltype(n)>(r("il2cpp_" #n));if(!n)throw std::runtime_error("Missing il2cpp_" #n)
        LOAD(domain_get);LOAD(domain_get_assemblies);LOAD(assembly_get_image);LOAD(image_get_name);
        LOAD(class_from_name);LOAD(class_get_parent);LOAD(class_get_name);LOAD(class_get_namespace);
        LOAD(class_get_method_from_name);LOAD(class_get_field_from_name);LOAD(field_get_type);LOAD(type_get_type);
        LOAD(field_get_value);LOAD(field_static_get_value);LOAD(runtime_invoke);
        LOAD(object_get_class);LOAD(object_unbox);LOAD(gchandle_new);LOAD(gchandle_get_target);LOAD(gchandle_free);
        LOAD(array_new);LOAD(array_length);LOAD(array_object_header_size);
        LOAD(thread_current);LOAD(thread_attach);LOAD(thread_detach);
#undef LOAD
#define OPT(n) n=reinterpret_cast<decltype(n)>(r("il2cpp_" #n))
        OPT(field_set_value);OPT(field_get_offset);OPT(field_get_flags);
        OPT(class_get_fields);OPT(field_get_name);
        OPT(class_get_type);OPT(type_get_object);
        OPT(class_get_methods);OPT(method_get_name);OPT(method_get_param_count);
        OPT(method_get_param);OPT(method_is_generic);
#undef OPT
        hasNativeLayout=(field_get_offset!=nullptr);
    }
};

struct Root {
    API* a=nullptr;std::uintptr_t h=0;
    Root()=default;Root(API&api,Obj o):a(&api),h(o?api.gchandle_new(o,false):0){if(o&&!h)throw std::runtime_error("GC handle failed");}
    Root(const Root&)=delete;Root&operator=(const Root&)=delete;
    Root(Root&&other)noexcept:a(other.a),h(other.h){other.a=nullptr;other.h=0;}
    Root&operator=(Root&&other)noexcept{if(this!=&other){reset();a=other.a;h=other.h;other.a=nullptr;other.h=0;}return *this;}
    ~Root(){reset();}
    Obj get()const{return h?a->gchandle_get_target(h):nullptr;}
    void reset(){if(h&&a){a->gchandle_free(h);h=0;}}
    void set(API&api,Obj o){auto next=o?api.gchandle_new(o,false):0;if(o&&!next)throw std::runtime_error("GC handle failed");reset();a=&api;h=next;}
};

struct WorldMarker {
    Vec3 anchor{};
    std::string color="green";
    std::string label;
    std::string key;
    int stage=-1;
    bool selected=false;
    bool showRing=true;
    bool showDistance=true;
};

struct RouteNode {
    Vec3 anchor{};
    std::string key;
    bool drop=false;
    int stage=0;
    bool goal=false;
};

struct ProjectedPt {
    float x=0,y=0,depth=0;
    bool ok=false;
};

bool isMiniControlName(const std::string&name){
    static const std::set<std::string> kMini={
        "ActorBattlePlayer","ActorCatchUpPlayer","ActorDuelPlayer","ActorFashionShowPlayer",
        "ActorInfiniteFloorPlayer","ActorKMGPlayer","ActorObbyPlayer","ActorPingPongPlayer",
        "ActorSmashOutPlayer","ActorSpeedUpPlayer","ActorTreasureHuntPlayer","ActorMiniGameControl"
    };
    return kMini.contains(name);
}

int parseDigits(const std::string&s,std::size_t pos,std::size_t len){
    if(pos+len>s.size())return -1;
    int v=0;
    for(std::size_t i=0;i<len;++i){
        if(!std::isdigit(static_cast<unsigned char>(s[pos+i])))return -1;
        v=v*10+(s[pos+i]-'0');
    }
    return v;
}

struct HoleLayout {
    std::string stageName;
    int stage=0;
    int limit=0;
    std::string variant;
};

[[maybe_unused]] bool parseHolePath(const std::vector<std::string>&path,HoleLayout&out){
    if(path.size()<7)return false;
    const std::size_t n=path.size();
    if(path[n-5]!="Hide_Stage"||path[n-2]!="Stamp")return false;
    const auto&sRoot=path[n-4];
    const auto&sBoard=path[n-3];
    const auto&sStamp=path[n-1];
    if(sRoot.size()!=15||sRoot.compare(0,6,"Stage_")!=0||sRoot.compare(9,6,"_Root")!=0)return false;
    int st=parseDigits(sRoot,6,2);
    int lim=parseDigits(sRoot,9,1);
    if(st<1||st>7||(lim!=1&&lim!=3&&lim!=5&&lim!=7))return false;
    if(sBoard.size()!=16||sBoard.compare(0,12,"Stage_Board_")!=0||sBoard[14]!='_')return false;
    if(parseDigits(sBoard,12,2)!=st)return false;
    char var=sBoard[15];
    if(var<'a'||var>'e')return false;
    if(sStamp.compare(0,15,"DropBoard_stage")!=0||sStamp.find("_Stamp_")==std::string::npos)return false;
    out={sRoot,st,lim,sBoard};
    return true;
}

[[maybe_unused]] bool parseHoleBoardPath(const std::vector<std::string>&path,int&stageOut,std::string&variantOut){
    if(path.size()<7)return false;
    const std::size_t n=path.size();
    if(path[n-5]!="Hide_Stage"||path[n-2]!="Board")return false;
    const auto&sBoard=path[n-3];
    if(sBoard.size()!=16||sBoard.compare(0,12,"Stage_Board_")!=0||sBoard[14]!='_')return false;
    int st=parseDigits(sBoard,12,2);
    char var=sBoard[15];
    if(st<1||st>7||var<'a'||var>'e')return false;
    stageOut=st;variantOut=sBoard;
    return true;
}

struct DoorChoice {
    std::string kind;
    std::string group;
    std::string side;
};

bool parseDoorIdentity(const std::vector<std::string>&path,DoorChoice&out){
    if(path.size()<5)return false;
    const std::size_t n=path.size();
    if(path[n-3]!="HideObject")return false;
    const auto&folder=path[n-2];
    const auto&leaf=path[n-1];
    if(folder=="Door_Root"){
        auto us=leaf.rfind('_');
        if(us==std::string::npos||us+2!=leaf.size()||leaf.compare(0,4,"door")!=0)return false;
        char s=leaf[us+1];
        if(s!='L'&&s!='C'&&s!='R')return false;
        out={"door",leaf.substr(0,us),std::string(1,s)};
        return true;
    }
    if(folder=="Obstacle_Root"&&(leaf=="plir_through"||leaf=="plir_wall")){
        out={"column","entrance-column",leaf=="plir_through"?"through":"wall"};
        return true;
    }
    if(folder=="Wall_Root"&&leaf.compare(0,21,"TwistRoad_Random_wall")==0){
        auto us=leaf.rfind('_');
        if(us!=std::string::npos&&us+2==leaf.size()&&(leaf[us+1]=='L'||leaf[us+1]=='R')){
            std::string num=leaf.substr(21,us-21);
            if(!num.empty()){
                out={"wall","wall"+num,std::string(1,leaf[us+1])};
                return true;
            }
        }
    }
    return false;
}

[[maybe_unused]] bool parseRoadVariant(const std::vector<std::string>&path,std::string&nameOut){
    if(path.size()<7)return false;
    const std::size_t n=path.size();
    if(path[n-2]!="Road_01_Root")return false;
    const auto&leaf=path[n-1];
    if(leaf.size()==7&&leaf.compare(0,5,"Road_")==0){
        int num=parseDigits(leaf,5,2);
        if(num>=1&&num<=27){nameOut=leaf;return true;}
    }
    return false;
}

[[maybe_unused]] int parseRoadLaneRow(const std::vector<std::string>&path){
    for(const auto&part:path){
        if(part.compare(0,10,"Road_line_")==0||part.compare(0,10,"road_line_")==0){
            if(part.size()==11&&part[10]>='1'&&part[10]<='3')return part[10]-'0';
            if(part.size()==12&&part[10]=='0'&&part[11]>='1'&&part[11]<='3')return part[11]-'0';
        }
    }
    return 0;
}

bool parseMazeVariant(const std::vector<std::string>&path,std::string&variantOut){
    for(std::size_t i=0;i+1<path.size();++i){
        if(path[i]=="RandomMaze_Root"){
            const auto&v=path[i+1];
            if(v.size()==17&&v.compare(0,13,"MazeCreate_S_")==0){
                int num=parseDigits(v,13,2);
                char dir=v[16];
                if(num>=1&&num<=22&&(dir=='R'||dir=='T'||dir=='B'||dir=='L')){
                    variantOut=v;return true;
                }
            }
        }
    }
    return false;
}

bool mazeSegmentHitsBox(Vec3 a,Vec3 b,const std::array<float,4>&box){
    double lo=0.0,hi=1.0;
    for(int axis=0;axis<2;++axis){
        double ak=axis==0?a.x:a.z,bk=axis==0?b.x:b.z;
        double delta=bk-ak,mn=box[axis],mx=box[axis+2];
        if(std::fabs(delta)<1e-12){
            if(ak<mn||ak>mx)return false;
        }else{
            double t0=(mn-ak)/delta,t1=(mx-ak)/delta;
            lo=std::max(lo,std::min(t0,t1));
            hi=std::min(hi,std::max(t0,t1));
            if(lo>hi)return false;
        }
    }
    return true;
}

struct MazePortalInfo {
    bool ready=false;
    bool fullyTraversable=false;
    std::string side;
    std::string barrierState="unverified";
    Vec3 goal{};
    Vec3 approach{};
    Vec3 inside{};
    Vec3 finishCenter{};
    std::vector<std::size_t> blockerIndices;
};

struct MazeGateRec {
    bool isStartWall=false;
    bool isFinishWall=false;
    bool solid=false;
    Bounds bounds{};
};

MazePortalInfo solveMazeGoalPortal(
    const std::vector<Bounds>&goalWalls,
    Vec3 center,
    const Bounds&floorB,
    float bodyRadius,
    float bodyHeight,
    const std::vector<MazeGateRec>&gates)
{
    MazePortalInfo out{};
    if(goalWalls.empty()||bodyRadius<=0.f||bodyHeight<=0.f)return out;
    const float y=floorB.center.y+floorB.extents.y;
    const float pad=bodyRadius;

    struct SideGroup {
        int axis=0;
        int tangent=2;
        int side=0;
        std::vector<Bounds> boxes;
    };
    std::map<std::string,SideGroup> groups;
    for(const Bounds&b:goalWalls){
        if(b.extents.x<=0.f||b.extents.y<=0.f||b.extents.z<=0.f)return out;
        int axis=(b.extents.x<b.extents.z)?0:2;
        int tangent=(axis==0)?2:0;
        float bAxis=(axis==0)?b.center.x:b.center.z;
        float cAxis=(axis==0)?center.x:center.z;
        float diff=bAxis-cAxis;
        if(std::fabs(diff)<1.0f)return out;
        int side=(diff>0.f)?1:-1;
        std::string key=std::string(axis==0?"X":"Z")+(side>0?"+":"-");
        auto&g=groups[key];
        g.axis=axis;g.tangent=tangent;g.side=side;
        g.boxes.push_back(b);
    }
    if(groups.size()!=4)return out;

    struct PortalCand {
        std::string side;
        int axis=0;
        float lo=0.f,hi=0.f,width=0.f;
        Vec3 approach{},inside{};
    };
    std::vector<PortalCand> portals;

    for(const auto&[key,g]:groups){
        float minPlane=1e30f,maxPlane=-1e30f;
        for(const Bounds&b:g.boxes){
            float p=(g.axis==0)?b.center.x:b.center.z;
            minPlane=std::min(minPlane,p);
            maxPlane=std::max(maxPlane,p);
        }
        if(maxPlane-minPlane>0.5f)return out;

        std::vector<std::pair<float,float>> intervals;
        for(const Bounds&b:g.boxes){
            if(b.center.y+b.extents.y>y+0.2f&&b.center.y-b.extents.y<y+bodyHeight-0.1f){
                float tc=(g.tangent==0)?b.center.x:b.center.z;
                float te=(g.tangent==0)?b.extents.x:b.extents.z;
                intervals.push_back({tc-te,tc+te});
            }
        }
        std::sort(intervals.begin(),intervals.end());
        std::vector<std::pair<float,float>> merged;
        for(const auto&r:intervals){
            if(!merged.empty()&&r.first<=merged.back().second+0.001f){
                merged.back().second=std::max(merged.back().second,r.second);
            }else{
                merged.push_back(r);
            }
        }

        for(std::size_t i=1;i<merged.size();++i){
            float lo=merged[i-1].second;
            float hi=merged[i].first;
            if(hi-lo<=2.f*pad+0.04f)continue;
            float middle=0.5f*(lo+hi);
            bool lintel=false;
            for(const Bounds&b:g.boxes){
                float tc=(g.tangent==0)?b.center.x:b.center.z;
                float te=(g.tangent==0)?b.extents.x:b.extents.z;
                if(b.center.y-b.extents.y>=y+bodyHeight+0.02f&&
                   tc-te<=lo+0.02f&&tc+te>=hi-0.02f){
                    lintel=true;
                    break;
                }
            }
            if(!lintel)continue;

            float outer=-1e30f,inner=1e30f;
            for(const Bounds&b:g.boxes){
                float ac=(g.axis==0)?b.center.x:b.center.z;
                float ae=(g.axis==0)?b.extents.x:b.extents.z;
                outer=std::max(outer,float(g.side)*ac+ae);
                inner=std::min(inner,float(g.side)*ac-ae);
            }
            Vec3 approach=center,inside=center;
            approach.y=inside.y=y;
            if(g.axis==0){
                approach.x=float(g.side)*(outer+pad+0.1f);
                inside.x=float(g.side)*(inner-pad-0.1f);
                approach.z=inside.z=middle;
            }else{
                approach.z=float(g.side)*(outer+pad+0.1f);
                inside.z=float(g.side)*(inner-pad-0.1f);
                approach.x=inside.x=middle;
            }
            portals.push_back({key,g.axis,lo,hi,hi-lo,approach,inside});
        }
    }

    if(portals.size()!=1)return out;
    const auto&p=portals[0];
    std::size_t finishGateCount=0;
    std::vector<std::size_t> blockers;
    for(std::size_t idx=0;idx<gates.size();++idx){
        const auto&g=gates[idx];
        if(!g.isFinishWall)continue;
        ++finishGateCount;
        if(g.solid&&g.bounds.center.y+g.bounds.extents.y>y+0.2f&&
           g.bounds.center.y-g.bounds.extents.y<y+bodyHeight-0.1f){
            std::array<float,4> rect{
                g.bounds.center.x-g.bounds.extents.x-pad,
                g.bounds.center.z-g.bounds.extents.z-pad,
                g.bounds.center.x+g.bounds.extents.x+pad,
                g.bounds.center.z+g.bounds.extents.z+pad
            };
            if(mazeSegmentHitsBox(p.approach,p.inside,rect)){
                blockers.push_back(idx);
            }
        }
    }
    const bool gateDataComplete=(finishGateCount==4);
    out.ready=true;
    out.side=p.side;
    out.approach=p.approach;
    out.inside=p.inside;
    out.finishCenter=center;
    out.blockerIndices=std::move(blockers);
    out.barrierState=!out.blockerIndices.empty()?"blocked":(gateDataComplete?"clear":"unverified");
    out.fullyTraversable=(out.barrierState=="clear");
    out.goal=out.fullyTraversable?out.inside:out.approach;
    return out;
}

struct MazeSolveResult {
    bool ready=false;
    bool retryFine=false;
    std::vector<RouteNode> nodes;
    std::vector<std::array<float,4>> obstacles;
    float cost=0.f;
};

MazeSolveResult solveMazeGridResolution(
    float meshStep,int meshW,int meshH,float meshX0,float meshZ0,
    const std::vector<float>&meshHeights,
    const std::vector<Bounds>&boxes,
    const Bounds&floorB,
    Vec3 origin,
    Vec3 goal,
    float bodyRadius,
    float bodyHeight,
    float planningStep,
    bool flexibleEndpoints)
{
    MazeSolveResult res{};
    if(bodyRadius<=0.f||bodyHeight<=0.f||planningStep<=0.f)return res;
    const float y=floorB.center.y+floorB.extents.y;
    const float step=planningStep;
    const float pad=bodyRadius;
    if(std::fabs(origin.y-y)>1.5f)return res;

    float gx0=meshX0,gz0=meshZ0;
    float gxmax=(meshW>=2)?(meshX0+float(meshW-1)*meshStep):(origin.x+2.f);
    float gzmax=(meshH>=2)?(meshZ0+float(meshH-1)*meshStep):(origin.z+2.f);
    if(meshW<2||meshH<2){
        gx0=std::min(origin.x,goal.x)-10.f;
        gz0=std::min(origin.z,goal.z)-10.f;
        gxmax=std::max(origin.x,goal.x)+10.f;
        gzmax=std::max(origin.z,goal.z)+10.f;
        for(const Bounds&b:boxes){
            if(b.extents.x<25.f&&b.extents.z<25.f){
                gx0=std::min(gx0,b.center.x-b.extents.x);
                gz0=std::min(gz0,b.center.z-b.extents.z);
                gxmax=std::max(gxmax,b.center.x+b.extents.x);
                gzmax=std::max(gzmax,b.center.z+b.extents.z);
            }
        }
    }

    const float x0=std::min({gx0,origin.x-2.f,goal.x-2.f});
    const float z0=std::min({gz0,origin.z-2.f,goal.z-2.f});
    const float xmax=std::max({gxmax,origin.x+2.f,goal.x+2.f});
    const float zmax=std::max({gzmax,origin.z+2.f,goal.z+2.f});
    const int w=int(std::ceil((xmax-x0)/step))+1;
    const int h=int(std::ceil((zmax-z0)/step))+1;
    if(w<=2||h<=2||w*h>80000)return res;

    std::vector<std::uint8_t> blocked(std::size_t(w*h),0);
    std::vector<std::array<float,4>>&obstacles=res.obstacles;
    constexpr float bucketSize=2.0f;
    std::map<std::pair<int,int>,std::vector<int>> buckets;

    auto stamp=[&](float ax,float az,float bx,float bz){
        int id=int(obstacles.size());
        obstacles.push_back({ax,az,bx,bz});
        int bzl=int(std::floor(std::max(az,z0-step*0.5f)/bucketSize));
        int bzh=int(std::floor(std::min(bz,zmax+step)/bucketSize));
        int bxl=int(std::floor(std::max(ax,x0-step*0.5f)/bucketSize));
        int bxh=int(std::floor(std::min(bx,xmax+step)/bucketSize));
        for(int bzIdx=bzl;bzIdx<=bzh;++bzIdx){
            for(int bxIdx=bxl;bxIdx<=bxh;++bxIdx){
                buckets[{bxIdx,bzIdx}].push_back(id);
            }
        }
        int loX=std::max(0,int(std::ceil((ax-x0)/step)));
        int hiX=std::min(w-1,int(std::floor((bx-x0)/step)));
        int loZ=std::max(0,int(std::ceil((az-z0)/step)));
        int hiZ=std::min(h-1,int(std::floor((bz-z0)/step)));
        for(int zz=loZ;zz<=hiZ;++zz){
            for(int xx=loX;xx<=hiX;++xx){
                blocked[std::size_t(zz*w+xx)]=1;
            }
        }
    };

    for(int i=0;i<w*h;++i){
        float x=x0+float(i%w)*step;
        float z=z0+float(i/w)*step;
        if(std::fabs(x-floorB.center.x)>floorB.extents.x-pad||
           std::fabs(z-floorB.center.z)>floorB.extents.z-pad){
            blocked[std::size_t(i)]=1;
        }
    }

    if(meshW>=2&&meshH>=2){
        for(std::size_t k=0;k<meshHeights.size();++k){
            float mh=meshHeights[k];
            if(!std::isfinite(mh)||mh<=y+0.2f)continue;
            float x=meshX0+float(int(k)%meshW)*meshStep;
            float z=meshZ0+float(int(k)/meshW)*meshStep;
            float r=pad+meshStep*0.5f;
            stamp(x-r,z-r,x+r,z+r);
        }
    }

    for(const Bounds&b:boxes){
        if(b.center.y+b.extents.y>y+0.2f&&b.center.y-b.extents.y<y+bodyHeight-0.1f){
            stamp(b.center.x-b.extents.x-pad,b.center.z-b.extents.z-pad,
                  b.center.x+b.extents.x+pad,b.center.z+b.extents.z+pad);
        }
    }

    auto segmentClear=[&](Vec3 a,Vec3 b)->bool{
        int lx=int(std::floor(std::min(a.x,b.x)/bucketSize));
        int hx=int(std::floor(std::max(a.x,b.x)/bucketSize));
        int lz=int(std::floor(std::min(a.z,b.z)/bucketSize));
        int hz=int(std::floor(std::max(a.z,b.z)/bucketSize));
        for(int zz=lz;zz<=hz;++zz){
            for(int xx=lx;xx<=hx;++xx){
                auto it=buckets.find({xx,zz});
                if(it==buckets.end())continue;
                for(int id:it->second){
                    if(mazeSegmentHitsBox(a,b,obstacles[std::size_t(id)]))return false;
                }
            }
        }
        return true;
    };

    auto point=[&](int k)->Vec3{
        return {x0+float(k%w)*step,y+0.04f,z0+float(k/w)*step};
    };
    auto index=[&](Vec3 p)->int{
        int xx=int(std::round((p.x-x0)/step));
        int zz=int(std::round((p.z-z0)/step));
        return (xx<0||xx>=w||zz<0||zz>=h)?-1:(zz*w+xx);
    };
    auto freeCell=[&](int xx,int zz)->bool{
        return xx>=0&&xx<w&&zz>=0&&zz<h&&!blocked[std::size_t(zz*w+xx)];
    };

    int smoothingWork=0;
    auto lineFree=[&](Vec3 a,Vec3 b,bool budgeted)->bool{
        double ax=double(a.x-x0)/step+0.5;
        double az=double(a.z-z0)/step+0.5;
        double bx=double(b.x-x0)/step+0.5;
        double bz=double(b.z-z0)/step+0.5;
        double dx=bx-ax,dz=bz-az;
        int sx=(dx>0.0)?1:((dx<0.0)?-1:0);
        int sz=(dz>0.0)?1:((dz<0.0)?-1:0);
        constexpr double eps=1e-9;
        int xx=int(std::floor(ax)),zz=int(std::floor(az));
        bool vertical=std::fabs(dx)<eps&&std::fabs(ax-std::round(ax))<eps;
        bool horizontal=std::fabs(dz)<eps&&std::fabs(az-std::round(az))<eps;
        auto check=[&](int cx,int cz)->bool{
            return freeCell(cx,cz)&&
                   (!vertical||freeCell(cx-1,cz))&&
                   (!horizontal||freeCell(cx,cz-1))&&
                   (!(vertical&&horizontal)||freeCell(cx-1,cz-1));
        };
        auto endpointFree=[&](double px,double pz)->bool{
            int ix=int(std::floor(px)),iz=int(std::floor(pz));
            std::vector<int> xs={ix},zs={iz};
            if(std::fabs(px-std::round(px))<eps)xs.push_back(ix-1);
            if(std::fabs(pz-std::round(pz))<eps)zs.push_back(iz-1);
            for(int cx:xs)for(int cz:zs)if(!freeCell(cx,cz))return false;
            return true;
        };
        if(!endpointFree(ax,az)||!endpointFree(bx,bz))return false;
        double tx=sx!=0?(1.0/std::fabs(dx)):std::numeric_limits<double>::infinity();
        double tz=sz!=0?(1.0/std::fabs(dz)):std::numeric_limits<double>::infinity();
        double mx=sx!=0?((sx>0?(double(xx+1)-ax):(ax-double(xx)))*tx):std::numeric_limits<double>::infinity();
        double mz=sz!=0?((sz>0?(double(zz+1)-az):(az-double(zz)))*tz):std::numeric_limits<double>::infinity();
        for(int n=0;n<=w+h+4;++n){
            if(budgeted&&++smoothingWork>120000)return false;
            if(!check(xx,zz))return false;
            if(std::min(mx,mz)>1.0-eps)return segmentClear(a,b);
            if(std::fabs(mx-mz)<eps){
                if(!freeCell(xx+sx,zz)||!freeCell(xx,zz+sz))return false;
                xx+=sx;zz+=sz;mx+=tx;mz+=tz;
            }else if(mx<mz){
                xx+=sx;mx+=tx;
            }else{
                zz+=sz;mz+=tz;
            }
        }
        return false;
    };

    const int nearestEnd=index(goal);
    bool snappedOrigin=false;
    auto endpointLinks=[&](Vec3 p,bool allowOriginSnap)->std::vector<std::pair<int,double>>{
        std::vector<std::pair<int,double>> links;
        int center=index(p);
        if(center<0||
           std::fabs(p.x-floorB.center.x)>floorB.extents.x+1.0f||
           std::fabs(p.z-floorB.center.z)>floorB.extents.z+1.0f){
            return links;
        }
        int reach=flexibleEndpoints?1:0;
        int cx=center%w,cz=center/w;
        for(int dz=-reach;dz<=reach;++dz){
            for(int dx=-reach;dx<=reach;++dx){
                int xx=cx+dx,zz=cz+dz;
                if(xx<0||xx>=w||zz<0||zz>=h)continue;
                int k=zz*w+xx;
                if(!blocked[std::size_t(k)]&&(!flexibleEndpoints||segmentClear(p,point(k)))){
                    double cost=flexibleEndpoints?(double(distXZ(p,point(k)))/double(step)):0.0;
                    links.push_back({k,cost});
                }
            }
        }
        if(links.empty()&&allowOriginSnap){
            for(int r=1;r<=8&&links.empty();++r){
                for(int dz=-r;dz<=r;++dz){
                    for(int dx=-r;dx<=r;++dx){
                        if(std::max(std::abs(dx),std::abs(dz))!=r)continue;
                        int xx=cx+dx,zz=cz+dz;
                        if(xx<0||xx>=w||zz<0||zz>=h)continue;
                        int k=zz*w+xx;
                        if(!blocked[std::size_t(k)]){
                            double cost=double(distXZ(p,point(k)))/double(step);
                            links.push_back({k,cost});
                        }
                    }
                }
            }
            if(!links.empty())snappedOrigin=true;
        }
        return links;
    };

    auto starts=endpointLinks(origin,flexibleEndpoints);
    auto ends=endpointLinks(goal,flexibleEndpoints);
    if(starts.empty()||ends.empty()){
        res.retryFine=true;
        return res;
    }
    if(!flexibleEndpoints&&
       (!lineFree(origin,point(starts[0].first),false)||
        !lineFree(point(ends[0].first),goal,false))){
        res.retryFine=true;
        return res;
    }

    std::map<int,double> endMap;
    for(const auto&[k,c]:ends)endMap[k]=c;

    std::vector<int> prev(std::size_t(w*h),-1);
    std::vector<double> dist(std::size_t(w*h),std::numeric_limits<double>::infinity());
    std::vector<std::uint8_t> closed(std::size_t(w*h),0);

    auto heuristic=[&](int k)->double{
        if(flexibleEndpoints)return double(distXZ(point(k),goal))/double(step);
        return std::hypot(double(k%w-nearestEnd%w),double(k/w-nearestEnd/w));
    };

    struct QNode {
        double f;
        double d;
        int k;
        bool operator>(const QNode&o)const{return f>o.f;}
    };
    std::priority_queue<QNode,std::vector<QNode>,std::greater<QNode>> pq;
    for(const auto&[k,d]:starts){
        prev[std::size_t(k)]=k;
        dist[std::size_t(k)]=d;
        pq.push({d+heuristic(k),d,k});
    }

    int endCell=-1;
    double bestTotal=std::numeric_limits<double>::infinity();
    while(!pq.empty()){
        auto[f,d,k]=pq.top();
        pq.pop();
        if(closed[std::size_t(k)]||d!=dist[std::size_t(k)])continue;
        if(std::isfinite(bestTotal)&&f>=bestTotal)break;
        closed[std::size_t(k)]=1;
        if(auto it=endMap.find(k);it!=endMap.end()){
            if(d+it->second<bestTotal){
                bestTotal=d+it->second;
                endCell=k;
            }
        }
        int xx=k%w,zz=k/w;
        for(int dz=-1;dz<=1;++dz){
            for(int dx=-1;dx<=1;++dx){
                if(!dx&&!dz)continue;
                int nx=xx+dx,nz=zz+dz;
                if(nx<0||nx>=w||nz<0||nz>=h)continue;
                int n=nz*w+nx;
                if(blocked[std::size_t(n)]||closed[std::size_t(n)])continue;
                if(dx&&dz&&(blocked[std::size_t(zz*w+nx)]||blocked[std::size_t(nz*w+xx)]))continue;
                double nd=d+((dx&&dz)?1.4142135623730951:1.0);
                if(!(nd+1e-10<dist[std::size_t(n)]))continue;
                if(!segmentClear(point(k),point(n)))continue;
                dist[std::size_t(n)]=nd;
                prev[std::size_t(n)]=k;
                pq.push({nd+heuristic(n),nd,n});
            }
        }
    }

    if(endCell<0){
        res.retryFine=true;
        return res;
    }

    std::vector<int> chain;
    for(int k=endCell;;k=prev[std::size_t(k)]){
        chain.push_back(k);
        if(prev[std::size_t(k)]==k||prev[std::size_t(k)]<0)break;
    }
    std::reverse(chain.begin(),chain.end());

    auto connectorClear=[&](Vec3 a,Vec3 b)->bool{
        return flexibleEndpoints?segmentClear(a,b):lineFree(a,b,false);
    };
    Vec3 originAnchor{origin.x,y+0.04f,origin.z};
    Vec3 goalAnchor{goal.x,y+0.04f,goal.z};
    const bool originDirect= !snappedOrigin && connectorClear(originAnchor,point(chain.front()));
    const bool goalDirect= connectorClear(point(chain.back()),goalAnchor);
    if((!originDirect||!goalDirect)&&!flexibleEndpoints){
        res.retryFine=true;
        return res;
    }

    std::vector<Vec3> anchors;
    anchors.reserve(chain.size()+2);
    if(originDirect)anchors.push_back(originAnchor);
    for(int k:chain)anchors.push_back(point(k));
    if(goalDirect)anchors.push_back(goalAnchor);

    std::vector<Vec3> reduced={anchors[0]};
    std::size_t at=0;
    while(at+1<anchors.size()){
        std::size_t next=at+1;
        if(smoothingWork<120000){
            for(std::size_t j=anchors.size()-1;j>at+1;--j){
                if(lineFree(anchors[at],anchors[j],true)){
                    next=j;
                    break;
                }
                if(smoothingWork>=120000)break;
            }
        }
        if(distXZ(reduced.back(),anchors[next])>1e-6f){
            reduced.push_back(anchors[next]);
        }
        at=next;
    }

    for(std::size_t i=1;i<reduced.size();++i){
        if(!segmentClear(reduced[i-1],reduced[i])){
            res.retryFine=true;
            return res;
        }
    }
    if(!originDirect&&distXZ(originAnchor,reduced.front())>1e-4f){
        reduced.insert(reduced.begin(),originAnchor);
    }
    if(!goalDirect&&distXZ(reduced.back(),goalAnchor)>1e-4f){
        reduced.push_back(goalAnchor);
    }

    float totalCost=0.f;
    for(std::size_t i=0;i<reduced.size();++i){
        if(i>0)totalCost+=distXZ(reduced[i-1],reduced[i]);
        res.nodes.push_back({reduced[i],"maze:"+std::to_string(i),false,0,i+1==reduced.size()});
    }
    res.ready=(res.nodes.size()>=2);
    res.cost=totalCost;
    return res;
}

MazeSolveResult solveMazeGrid(
    float meshStep,int meshW,int meshH,float meshX0,float meshZ0,
    const std::vector<float>&meshHeights,
    const std::vector<Bounds>&boxes,
    const Bounds&floorB,
    Vec3 origin,
    Vec3 goal,
    float bodyRadius,
    float bodyHeight)
{
    auto first=solveMazeGridResolution(meshStep,meshW,meshH,meshX0,meshZ0,meshHeights,boxes,floorB,origin,goal,bodyRadius,bodyHeight,meshStep,false);
    if(first.ready||!first.retryFine)return first;
    return solveMazeGridResolution(meshStep,meshW,meshH,meshX0,meshZ0,meshHeights,boxes,floorB,origin,goal,bodyRadius,bodyHeight,meshStep*0.5f,true);
}

struct MazePresetGrid { const char* variant; int w; int h; float x0; float z0; const std::uint8_t* bits; };
static constexpr std::uint8_t kMazeBits_03_R[595] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x01,0x10,0x00,0x00,0x80,0x08,0x00,0x00,0x80,0x00,0x08,0x00,0x00,0x40,0x04,0x00,0x00,0x40,0x00,0x04,0x00,0x00,0x20,0x02,0x00,0x00,0x20,0x00,0x02,0x00,0xf0,0x11,0xf1,0x1f,0xff,0x1f,0x11,0x1f,0x1f,0x80,0x88,0x08,0x08,0x00,0x88,0x08,0x08,0x08,0x40,0x44,0x04,0x04,0x00,0x44,0x04,0x04,0x04,0x20,0x22,0x02,0x02,0x00,0x22,0x02,0x02,0x02,0x10,0x11,0x11,0xff,0x1f,0x11,0x1f,0x11,0x01,0x80,0x00,0x08,0x08,0x08,0x88,0x88,0x88,0x00,0x40,0x00,0x04,0x04,0x04,0x44,0x44,0x44,0x00,0x20,0x00,0x02,0x02,0x02,0x22,0x22,0x22,0x00,0xff,0xff,0x1f,0xf1,0xff,0x11,0x11,0xf1,0x81,0x80,0x00,0x80,0x00,0x08,0x08,0x88,0x00,0x40,0x40,0x00,0x40,0x00,0x04,0x04,0x44,0x00,0x20,0x20,0x00,0x20,0x00,0x02,0x02,0x22,0x00,0x10,0xf1,0x1f,0xff,0xf1,0xf1,0xf1,0x1f,0x01,0x80,0x00,0x08,0x00,0x08,0x88,0x00,0x88,0x00,0x40,0x00,0x04,0x00,0x04,0x44,0x00,0x44,0x00,0x20,0x00,0x02,0x00,0x02,0x22,0x00,0x22,0xf0,0xf1,0x1f,0xff,0x1f,0xf1,0xf1,0x1f,0x11,0x00,0x00,0x08,0x00,0x88,0x08,0x08,0x88,0x08,0x00,0x00,0x04,0x00,0x44,0x04,0x04,0x44,0x04,0x00,0x00,0x02,0x00,0x22,0x02,0x02,0x22,0x02,0xf0,0x1f,0xff,0x1f,0x1f,0xf1,0x11,0xf1,0x01,0x88,0x00,0x00,0x08,0x80,0x00,0x88,0x00,0x00,0x44,0x00,0x00,0x04,0x40,0x00,0x44,0x00,0x00,0x22,0x00,0x00,0x02,0x20,0x00,0x22,0x00,0x00,0x11,0xff,0xff,0x01,0x10,0xff,0xf1,0x1f,0x00,0x08,0x80,0x00,0x00,0x00,0x08,0x08,0x00,0x00,0x04,0x40,0x00,0x00,0x00,0x04,0x04,0x00,0x00,0x02,0x20,0x00,0x00,0x00,0x02,0x02,0x00,0xff,0x1f,0x11,0x01,0x00,0xf0,0x11,0xf1,0x1f,0x00,0x88,0x80,0x00,0x00,0x08,0x88,0x08,0x00,0x00,0x44,0x40,0x00,0x00,0x04,0x44,0x04,0x00,0x00,0x22,0x20,0x00,0x00,0x02,0x22,0x02,0x00,0x1f,0xff,0x1f,0x00,0xf0,0xf1,0x1f,0x1f,0x80,0x08,0x80,0x00,0x00,0x00,0x08,0x88,0x00,0x40,0x04,0x40,0x00,0x00,0x00,0x04,0x44,0x00,0x20,0x02,0x20,0x00,0x00,0x00,0x02,0x22,0x00,0x10,0xff,0x11,0x01,0x01,0xf0,0x11,0x11,0x01,0x08,0x80,0x80,0x80,0x00,0x80,0x88,0x88,0x00,0x04,0x40,0x40,0x40,0x00,0x40,0x44,0x44,0x00,0x02,0x20,0x20,0x20,0x00,0x20,0x22,0x22,0x00,0xf1,0xf1,0xff,0x11,0xff,0x11,0x11,0x11,0x80,0x80,0x00,0x00,0x08,0x88,0x80,0x08,0x08,0x40,0x40,0x00,0x00,0x04,0x44,0x40,0x04,0x04,0x20,0x20,0x00,0x00,0x02,0x22,0x20,0x02,0x02,0xf0,0xf1,0xff,0xff,0x1f,0xf1,0x1f,0xff,0x01,0x88,0x80,0x08,0x00,0x88,0x00,0x08,0x00,0x00,0x44,0x40,0x04,0x00,0x44,0x00,0x04,0x00,0x00,0x22,0x20,0x02,0x00,0x22,0x00,0x02,0x00,0x00,0xf1,0x11,0xf1,0x1f,0xf1,0xf1,0xf1,0xff,0x81,0x88,0x08,0x08,0x88,0x80,0x08,0x00,0x08,0x40,0x44,0x04,0x04,0x44,0x40,0x04,0x00,0x04,0x20,0x22,0x02,0x02,0x22,0x20,0x02,0x00,0x02,0x10,0x11,0xff,0x11,0xf1,0x11,0x11,0x1f,0x01,0x88,0x08,0x80,0x08,0x80,0x08,0x88,0x88,0x00,0x44,0x04,0x40,0x04,0x40,0x04,0x44,0x44,0x00,0x22,0x02,0x20,0x02,0x20,0x02,0x22,0x22,0xf0,0x11,0xff,0x11,0xff,0x11,0xff,0x11,0x11,0x00,0x08,0x00,0x80,0x00,0x08,0x00,0x88,0x08,0x00,0x04,0x00,0x40,0x00,0x04,0x00,0x44,0x04,0x00,0x02,0x00,0x20,0x00,0x02,0x00,0x22,0x02,0xf0,0xff,0xff,0xff,0x1f,0xff,0xff,0x11,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_06_R[631] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x10,0x00,0x00,0x00,0x00,0x01,0x00,0x80,0x00,0x08,0x00,0x00,0x00,0x80,0x00,0x00,0x40,0x00,0x04,0x00,0x00,0x00,0x40,0x00,0x00,0x20,0x00,0x02,0x00,0x00,0x00,0x20,0x00,0xf0,0x11,0x1f,0x11,0xff,0xff,0xf1,0xf1,0x11,0x80,0x80,0x80,0x08,0x00,0x80,0x80,0x00,0x08,0x40,0x40,0x40,0x04,0x00,0x40,0x40,0x00,0x04,0x20,0x20,0x20,0x02,0x00,0x20,0x20,0x00,0x02,0xf0,0x1f,0xff,0xf1,0xff,0x11,0xf1,0xff,0x01,0x00,0x08,0x08,0x08,0x08,0x88,0x80,0x80,0x00,0x00,0x04,0x04,0x04,0x04,0x44,0x40,0x40,0x00,0x00,0x02,0x02,0x02,0x02,0x22,0x20,0x20,0x00,0xf1,0x1f,0xf1,0x1f,0xf1,0x1f,0x11,0xf1,0x81,0x00,0x88,0x00,0x88,0x00,0x88,0x80,0x00,0x40,0x00,0x44,0x00,0x44,0x00,0x44,0x40,0x00,0x20,0x00,0x22,0x00,0x22,0x00,0x22,0x20,0x00,0xf0,0x1f,0xf1,0x1f,0xf1,0x1f,0xff,0xff,0x01,0x08,0x08,0x08,0x80,0x00,0x08,0x08,0x80,0x00,0x04,0x04,0x04,0x40,0x00,0x04,0x04,0x40,0x00,0x02,0x02,0x02,0x20,0x00,0x02,0x02,0x20,0x00,0xf1,0xff,0xf1,0x1f,0x1f,0x1f,0xf1,0x11,0x80,0x00,0x00,0x08,0x88,0x08,0x88,0x88,0x00,0x40,0x00,0x00,0x04,0x44,0x04,0x44,0x44,0x00,0x20,0x00,0x00,0x02,0x22,0x02,0x22,0x22,0x00,0xf0,0xff,0xf1,0xf1,0x11,0x1f,0x11,0xf1,0x01,0x00,0x08,0x08,0x00,0x00,0x88,0x80,0x00,0x00,0x00,0x04,0x04,0x00,0x00,0x44,0x40,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x22,0x20,0x00,0xf0,0x1f,0xf1,0xff,0x01,0xf0,0xf1,0x1f,0x1f,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x04,0x04,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x02,0x02,0x10,0xff,0xff,0x01,0x00,0xf0,0x1f,0x1f,0x01,0x88,0x00,0x08,0x00,0x00,0x00,0x08,0x88,0x00,0x44,0x00,0x04,0x00,0x00,0x00,0x04,0x44,0x00,0x22,0x00,0x02,0x00,0x00,0x00,0x02,0x22,0x00,0x11,0x1f,0xf1,0x01,0x00,0x1f,0xff,0x11,0x80,0x80,0x88,0x08,0x00,0x80,0x08,0x00,0x08,0x40,0x40,0x44,0x04,0x00,0x40,0x04,0x00,0x04,0x20,0x20,0x22,0x02,0x00,0x20,0x02,0x00,0x02,0x10,0x1f,0x11,0x11,0x00,0x11,0xff,0xff,0x01,0x08,0x80,0x88,0x08,0x80,0x08,0x08,0x00,0x00,0x04,0x40,0x44,0x04,0x40,0x04,0x04,0x00,0x00,0x02,0x20,0x22,0x02,0x20,0x02,0x02,0x00,0x00,0xf1,0x11,0x11,0x11,0x1f,0xf1,0xf1,0x1f,0x00,0x08,0x88,0x08,0x08,0x80,0x00,0x00,0x08,0x00,0x04,0x44,0x04,0x04,0x40,0x00,0x00,0x04,0x00,0x02,0x22,0x02,0x02,0x20,0x00,0x00,0x02,0xf0,0x11,0x11,0xf1,0xff,0xff,0xff,0x1f,0x01,0x80,0x88,0x88,0x08,0x00,0x00,0x80,0x80,0x00,0x40,0x44,0x44,0x04,0x00,0x00,0x40,0x40,0x00,0x20,0x22,0x22,0x02,0x00,0x00,0x20,0x20,0xf0,0x1f,0x11,0x11,0xf1,0xff,0xff,0xf1,0x11,0x00,0x80,0x08,0x08,0x08,0x80,0x80,0x80,0x08,0x00,0x40,0x04,0x04,0x04,0x40,0x40,0x40,0x04,0x00,0x20,0x02,0x02,0x02,0x20,0x20,0x20,0x02,0xf0,0x1f,0xf1,0xff,0x11,0x1f,0xf1,0x11,0x01,0x00,0x88,0x08,0x00,0x88,0x80,0x08,0x88,0x00,0x00,0x44,0x04,0x00,0x44,0x40,0x04,0x44,0x00,0x00,0x22,0x02,0x00,0x22,0x20,0x02,0x22,0x00,0x1f,0x11,0xf1,0xf1,0x1f,0x1f,0xf1,0x1f,0x80,0x88,0x80,0x88,0x80,0x80,0x08,0x08,0x08,0x40,0x44,0x40,0x44,0x40,0x40,0x04,0x04,0x04,0x20,0x22,0x20,0x22,0x20,0x20,0x02,0x02,0x02,0x10,0xf1,0x1f,0xf1,0x11,0x1f,0xff,0x11,0x01,0x80,0x00,0x00,0x80,0x00,0x00,0x00,0x08,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x00,0x04,0x00,0x20,0x00,0x00,0x20,0x00,0x00,0x00,0x02,0x00,0x10,0x00,0x00,0x10,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_09_T[631] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x01,0x08,0x00,0x00,0x80,0x80,0x00,0x00,0x80,0x00,0x04,0x00,0x00,0x40,0x40,0x00,0x00,0x40,0x00,0x02,0x00,0x00,0x20,0x20,0x00,0x00,0x20,0x00,0x1f,0xff,0xf1,0x11,0x11,0xf1,0xff,0x11,0x00,0x88,0x00,0x08,0x88,0x88,0x08,0x00,0x08,0x00,0x44,0x00,0x04,0x44,0x44,0x04,0x00,0x04,0x00,0x22,0x00,0x02,0x22,0x22,0x02,0x00,0x02,0x1f,0x11,0xff,0xf1,0x1f,0x11,0xf1,0xff,0x01,0x88,0x88,0x80,0x00,0x08,0x88,0x00,0x80,0x00,0x44,0x44,0x40,0x00,0x04,0x44,0x00,0x40,0x00,0x22,0x22,0x20,0x00,0x02,0x22,0x00,0x20,0x00,0xf1,0x11,0xff,0x1f,0xff,0x11,0xff,0x11,0x80,0x00,0x08,0x00,0x88,0x00,0x08,0x80,0x08,0x40,0x00,0x04,0x00,0x44,0x00,0x04,0x40,0x04,0x20,0x00,0x02,0x00,0x22,0x00,0x02,0x20,0x02,0xf0,0x11,0xff,0x1f,0x11,0xff,0xf1,0x11,0x01,0x80,0x08,0x00,0x08,0x80,0x08,0x08,0x88,0x00,0x40,0x04,0x00,0x04,0x40,0x04,0x04,0x44,0x00,0x20,0x02,0x00,0x02,0x20,0x02,0x02,0x22,0x00,0x11,0xff,0x11,0xff,0x1f,0xff,0xf1,0x11,0x80,0x08,0x80,0x08,0x00,0x80,0x80,0x08,0x08,0x40,0x04,0x40,0x04,0x00,0x40,0x40,0x04,0x04,0x20,0x02,0x20,0x02,0x00,0x20,0x20,0x02,0x02,0x1f,0xff,0xf1,0xff,0xff,0xff,0x11,0x1f,0x01,0x00,0x88,0x08,0x08,0x00,0x00,0x08,0x88,0x00,0x00,0x44,0x04,0x04,0x00,0x00,0x04,0x44,0x00,0x00,0x22,0x02,0x02,0x00,0x00,0x02,0x22,0x00,0x1f,0x11,0x11,0x01,0xf0,0x1f,0x1f,0x11,0x00,0x88,0x88,0x08,0x00,0x00,0x88,0x88,0x00,0x00,0x44,0x44,0x04,0x00,0x00,0x44,0x44,0x00,0x00,0x22,0x22,0x02,0x00,0x00,0x22,0x22,0x00,0x10,0x11,0x11,0x01,0x00,0xf0,0x11,0xf1,0x01,0x88,0x88,0x80,0x00,0x00,0x08,0x80,0x08,0x00,0x44,0x44,0x40,0x00,0x00,0x04,0x40,0x04,0x00,0x22,0x22,0x20,0x00,0x00,0x02,0x20,0x02,0xf0,0x11,0xf1,0x1f,0x00,0x00,0xf1,0x1f,0xf1,0x01,0x08,0x00,0x00,0x00,0x80,0x88,0x80,0x00,0x00,0x04,0x00,0x00,0x00,0x40,0x44,0x40,0x00,0x00,0x02,0x00,0x00,0x00,0x20,0x22,0x20,0x00,0xf0,0xff,0x1f,0x01,0x01,0x10,0x11,0xff,0x1f,0x00,0x08,0x88,0x80,0x00,0x00,0x08,0x00,0x00,0x00,0x04,0x44,0x40,0x00,0x00,0x04,0x00,0x00,0x00,0x02,0x22,0x20,0x00,0x00,0x02,0x00,0x00,0x1f,0x11,0xf1,0xf1,0xff,0x1f,0xff,0x1f,0x00,0x08,0x88,0x80,0x00,0x80,0x00,0x00,0x08,0x00,0x04,0x44,0x40,0x00,0x40,0x00,0x00,0x04,0x00,0x02,0x22,0x20,0x00,0x20,0x00,0x00,0x02,0x1f,0xff,0xf1,0xff,0xff,0x11,0xff,0x1f,0x01,0x08,0x80,0x08,0x08,0x00,0x08,0x08,0x80,0x00,0x04,0x40,0x04,0x04,0x00,0x04,0x04,0x40,0x00,0x02,0x20,0x02,0x02,0x00,0x02,0x02,0x20,0x00,0x1f,0x11,0x11,0xf1,0xff,0xff,0xf1,0x11,0x00,0x80,0x08,0x08,0x08,0x00,0x00,0x88,0x08,0x00,0x40,0x04,0x04,0x04,0x00,0x00,0x44,0x04,0x00,0x20,0x02,0x02,0x02,0x00,0x00,0x22,0x02,0xf0,0xff,0xff,0xff,0xff,0x11,0xff,0x11,0x01,0x08,0x80,0x00,0x08,0x80,0x88,0x00,0x08,0x00,0x04,0x40,0x00,0x04,0x40,0x44,0x00,0x04,0x00,0x02,0x20,0x00,0x02,0x20,0x22,0x00,0x02,0x00,0x11,0x11,0x1f,0xf1,0xf1,0x11,0xff,0x1f,0x80,0x88,0x88,0x88,0x80,0x80,0x08,0x00,0x08,0x40,0x44,0x44,0x44,0x40,0x40,0x04,0x00,0x04,0x20,0x22,0x22,0x22,0x20,0x20,0x02,0x00,0x02,0xf0,0x11,0x11,0xf1,0xf1,0x11,0xf1,0xf1,0x01,0x00,0x08,0x08,0x00,0x08,0x80,0x80,0x00,0x00,0x00,0x04,0x04,0x00,0x04,0x40,0x40,0x00,0x00,0x00,0x02,0x02,0x00,0x02,0x20,0x20,0x00,0x00,0x00,0x01,0x01,0x00,0x01,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_10_T[631] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x02,0xf0,0x11,0x11,0xff,0xff,0xf1,0x1f,0xf1,0x11,0x80,0x80,0x88,0x00,0x80,0x00,0x88,0x00,0x08,0x40,0x40,0x44,0x00,0x40,0x00,0x44,0x00,0x04,0x20,0x20,0x22,0x00,0x20,0x00,0x22,0x00,0x02,0xf0,0x1f,0xf1,0x11,0xff,0x1f,0x1f,0xff,0x01,0x00,0x80,0x80,0x88,0x00,0x08,0x88,0x80,0x00,0x00,0x40,0x40,0x44,0x00,0x04,0x44,0x40,0x00,0x00,0x20,0x20,0x22,0x00,0x02,0x22,0x20,0x00,0x11,0x1f,0xf1,0x11,0x1f,0x1f,0xf1,0x11,0x80,0x88,0x80,0x08,0x88,0x08,0x88,0x00,0x08,0x40,0x44,0x40,0x04,0x44,0x04,0x44,0x00,0x04,0x20,0x22,0x20,0x02,0x22,0x02,0x22,0x00,0x02,0x10,0x11,0x1f,0xf1,0x11,0x1f,0xf1,0xf1,0x01,0x88,0x08,0x88,0x00,0x08,0x08,0x08,0x08,0x00,0x44,0x04,0x44,0x00,0x04,0x04,0x04,0x04,0x00,0x22,0x02,0x22,0x00,0x02,0x02,0x02,0x02,0x00,0x11,0xff,0x11,0xff,0x11,0xff,0xff,0x1f,0x80,0x88,0x00,0x08,0x08,0x08,0x00,0x80,0x08,0x40,0x44,0x00,0x04,0x04,0x04,0x00,0x40,0x04,0x20,0x22,0x00,0x02,0x02,0x02,0x00,0x20,0x02,0xf0,0x11,0xff,0x1f,0xf1,0x1f,0x11,0x11,0x01,0x80,0x80,0x00,0x08,0x08,0x88,0x88,0x80,0x00,0x40,0x40,0x00,0x04,0x04,0x44,0x44,0x40,0x00,0x20,0x20,0x00,0x02,0x02,0x22,0x22,0x20,0x00,0xf1,0x1f,0x1f,0x01,0x01,0x11,0xf1,0x1f,0x80,0x80,0x80,0x08,0x00,0x80,0x88,0x08,0x08,0x40,0x40,0x40,0x04,0x00,0x40,0x44,0x04,0x04,0x20,0x20,0x20,0x02,0x00,0x20,0x22,0x02,0x02,0xff,0xf1,0x1f,0x01,0x00,0x10,0x1f,0x11,0x1f,0x00,0x08,0x88,0x00,0x00,0x08,0x08,0x08,0x00,0x00,0x04,0x44,0x00,0x00,0x04,0x04,0x04,0x00,0x00,0x02,0x22,0x00,0x00,0x02,0x02,0x02,0x00,0xff,0x1f,0x11,0x00,0x00,0x1f,0xf1,0x1f,0x00,0x80,0x80,0x08,0x00,0x00,0x80,0x80,0x00,0x00,0x40,0x40,0x04,0x00,0x00,0x40,0x40,0x00,0x00,0x20,0x20,0x02,0x00,0x00,0x20,0x20,0x00,0xf0,0x11,0x1f,0x01,0x00,0xf1,0xff,0x11,0x01,0x88,0x00,0x80,0x00,0x80,0x08,0x08,0x88,0x00,0x44,0x00,0x40,0x00,0x40,0x04,0x04,0x44,0x00,0x22,0x00,0x20,0x00,0x20,0x02,0x02,0x22,0x00,0xf1,0xff,0x11,0xff,0x1f,0xf1,0xf1,0x11,0x80,0x00,0x00,0x08,0x00,0x80,0x08,0x08,0x08,0x40,0x00,0x00,0x04,0x00,0x40,0x04,0x04,0x04,0x20,0x00,0x00,0x02,0x00,0x20,0x02,0x02,0x02,0x10,0xff,0x1f,0xff,0xff,0x1f,0xff,0xf1,0x01,0x08,0x80,0x88,0x00,0x00,0x00,0x80,0x08,0x00,0x04,0x40,0x44,0x00,0x00,0x00,0x40,0x04,0x00,0x02,0x20,0x22,0x00,0x00,0x00,0x20,0x02,0x00,0x1f,0x1f,0x1f,0x1f,0x1f,0xf1,0x11,0xff,0x01,0x88,0x80,0x80,0x80,0x88,0x08,0x08,0x00,0x00,0x44,0x40,0x40,0x40,0x44,0x04,0x04,0x00,0x00,0x22,0x20,0x20,0x20,0x22,0x02,0x02,0x00,0x1f,0x11,0x1f,0x1f,0x1f,0x11,0x11,0xf1,0x01,0x88,0x08,0x80,0x80,0x08,0x88,0x88,0x80,0x00,0x44,0x04,0x40,0x40,0x04,0x44,0x44,0x40,0x00,0x22,0x02,0x20,0x20,0x02,0x22,0x22,0x20,0x00,0xf1,0xff,0x1f,0x1f,0xf1,0x1f,0xf1,0x1f,0x80,0x08,0x00,0x80,0x08,0x08,0x80,0x00,0x00,0x40,0x04,0x00,0x40,0x04,0x04,0x40,0x00,0x00,0x20,0x02,0x00,0x20,0x02,0x02,0x20,0x00,0x00,0x10,0xf1,0xff,0x1f,0xf1,0x1f,0xff,0xff,0x01,0x00,0x08,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_11_T[631] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x10,0x00,0x00,0x00,0x80,0x00,0x80,0x00,0x00,0x08,0x00,0x00,0x00,0x40,0x00,0x40,0x00,0x00,0x04,0x00,0x00,0x00,0x20,0x00,0x20,0x00,0x00,0x02,0x00,0x00,0xe0,0x11,0xff,0x11,0xff,0x1f,0x11,0xff,0x1f,0x80,0x08,0x88,0x08,0x80,0x08,0x08,0x00,0x08,0x40,0x04,0x44,0x04,0x40,0x04,0x04,0x00,0x04,0x20,0x02,0x22,0x02,0x20,0x02,0x02,0x00,0x02,0x10,0x1f,0x11,0xff,0x11,0xff,0x1f,0x1f,0x01,0x08,0x80,0x80,0x00,0x00,0x08,0x08,0x88,0x00,0x04,0x40,0x40,0x00,0x00,0x04,0x04,0x44,0x00,0x02,0x20,0x20,0x00,0x00,0x02,0x02,0x22,0x00,0xff,0x1f,0xff,0xf1,0xff,0x11,0xff,0x11,0x80,0x00,0x80,0x80,0x08,0x80,0x08,0x08,0x08,0x40,0x00,0x40,0x40,0x04,0x40,0x04,0x04,0x04,0x20,0x00,0x20,0x20,0x02,0x20,0x02,0x02,0x02,0x10,0xff,0x1f,0x11,0xf1,0xff,0x1f,0xf1,0x01,0x88,0x00,0x80,0x80,0x80,0x80,0x80,0x88,0x00,0x44,0x00,0x40,0x40,0x40,0x40,0x40,0x44,0x00,0x22,0x00,0x20,0x20,0x20,0x20,0x20,0x22,0x00,0xf1,0x1f,0xf1,0x11,0xf1,0x11,0x1f,0x11,0x00,0x00,0x88,0x80,0x88,0x08,0x88,0x80,0x00,0x00,0x00,0x44,0x40,0x44,0x04,0x44,0x40,0x00,0x00,0x00,0x22,0x20,0x22,0x02,0x22,0x20,0x00,0xf0,0x1f,0xff,0x11,0x1f,0xf1,0x11,0xff,0x1f,0x00,0x08,0x80,0x08,0x80,0x80,0x08,0x08,0x00,0x00,0x04,0x40,0x04,0x40,0x40,0x04,0x04,0x00,0x00,0x02,0x20,0x02,0x20,0x20,0x02,0x02,0xf0,0x1f,0xff,0x11,0x01,0x10,0x11,0x11,0x11,0x80,0x00,0x80,0x00,0x00,0x80,0x88,0x08,0x08,0x40,0x00,0x40,0x00,0x00,0x40,0x44,0x04,0x04,0x20,0x00,0x20,0x00,0x00,0x20,0x22,0x02,0x02,0xf0,0xff,0x1f,0x1f,0x00,0x10,0x11,0xff,0x01,0x00,0x00,0x08,0x00,0x00,0x08,0x08,0x88,0x00,0x00,0x00,0x04,0x00,0x00,0x04,0x04,0x44,0x00,0x00,0x00,0x02,0x00,0x00,0x02,0x02,0x22,0x00,0xff,0x1f,0x11,0x00,0x00,0x1f,0x1f,0x11,0x00,0x00,0x08,0x08,0x00,0x00,0x08,0x08,0x08,0x00,0x00,0x04,0x04,0x00,0x00,0x04,0x04,0x04,0x00,0x00,0x02,0x02,0x00,0x00,0x02,0x02,0x02,0xf0,0xff,0xff,0x0f,0x11,0x1f,0x1f,0x1f,0x01,0x08,0x08,0x08,0x80,0x08,0x08,0x08,0x80,0x00,0x04,0x04,0x04,0x40,0x04,0x04,0x04,0x40,0x00,0x02,0x02,0x02,0x20,0x02,0x02,0x02,0x20,0x00,0x11,0x11,0xf1,0x11,0x11,0x1f,0xff,0x1f,0x80,0x08,0x08,0x08,0x88,0x08,0x88,0x00,0x08,0x40,0x04,0x04,0x04,0x44,0x04,0x44,0x00,0x04,0x20,0x02,0x02,0x02,0x22,0x02,0x22,0x00,0x02,0x10,0xff,0xff,0xf1,0x11,0x1f,0x11,0x1f,0x01,0x08,0x00,0x88,0x08,0x88,0x08,0x08,0x88,0x00,0x04,0x00,0x44,0x04,0x44,0x04,0x04,0x44,0x00,0x02,0x00,0x22,0x02,0x22,0x02,0x02,0x22,0x00,0xff,0x1f,0x11,0x11,0x11,0xff,0x11,0x11,0x00,0x80,0x80,0x80,0x88,0x00,0x00,0x88,0x00,0x00,0x40,0x40,0x40,0x44,0x00,0x00,0x44,0x00,0x00,0x20,0x20,0x20,0x22,0x00,0x00,0x22,0x00,0x10,0x11,0x1f,0xff,0xf1,0x1f,0xff,0xf1,0x01,0x88,0x80,0x00,0x00,0x00,0x08,0x80,0x80,0x00,0x44,0x40,0x00,0x00,0x00,0x04,0x40,0x40,0x00,0x22,0x20,0x00,0x00,0x00,0x02,0x20,0x20,0x00,0xf1,0x1f,0xff,0xff,0xff,0xff,0xf1,0x11,0x80,0x08,0x00,0x08,0x00,0x80,0x80,0x00,0x08,0x40,0x04,0x00,0x04,0x00,0x40,0x40,0x00,0x04,0x20,0x02,0x00,0x02,0x00,0x20,0x20,0x00,0x02,0x10,0xff,0xff,0xf1,0xff,0x11,0xf1,0xff,0x01,0x08,0x00,0x00,0x00,0x80,0x80,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x40,0x40,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_12_T[613] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0xf0,0xf1,0x1f,0xf1,0x1f,0xf1,0xff,0xf1,0x01,0x04,0x04,0x44,0x04,0x44,0x00,0x40,0x44,0x00,0x01,0x01,0x11,0x01,0x11,0x00,0x10,0x11,0x40,0x40,0x40,0x44,0x40,0x04,0x00,0x44,0x04,0x10,0x1f,0x1f,0x1f,0x11,0x1f,0xff,0x11,0x01,0x04,0x04,0x04,0x44,0x40,0x40,0x00,0x44,0x00,0x01,0x01,0x01,0x11,0x10,0x10,0x00,0x11,0x40,0x40,0x40,0x40,0x04,0x04,0x04,0x40,0x04,0xf0,0x11,0xf1,0x11,0xf1,0xf1,0xf1,0x1f,0x01,0x00,0x44,0x40,0x44,0x04,0x40,0x04,0x04,0x00,0x00,0x11,0x10,0x11,0x01,0x10,0x01,0x01,0x00,0x40,0x04,0x44,0x44,0x00,0x44,0x40,0x00,0xf0,0xff,0x11,0x11,0x1f,0xff,0x11,0x11,0x0f,0x00,0x00,0x44,0x04,0x44,0x40,0x44,0x04,0x00,0x00,0x00,0x11,0x01,0x11,0x10,0x11,0x01,0x00,0x00,0x40,0x44,0x40,0x04,0x44,0x44,0x00,0xf0,0xff,0xff,0x11,0x11,0x11,0x11,0xf1,0x01,0x04,0x44,0x04,0x44,0x44,0x04,0x44,0x40,0x00,0x01,0x11,0x01,0x11,0x11,0x01,0x11,0x10,0x40,0x40,0x44,0x40,0x44,0x44,0x40,0x04,0x04,0x10,0x11,0x11,0x1f,0x11,0xf1,0x1f,0x1f,0x01,0x44,0x40,0x04,0x44,0x40,0x00,0x44,0x40,0x00,0x11,0x10,0x01,0x11,0x10,0x00,0x11,0x10,0x40,0x04,0x44,0x40,0x04,0x04,0x40,0x04,0x04,0x10,0xff,0xf1,0x11,0x01,0xf1,0x1f,0xff,0x01,0x40,0x00,0x40,0x00,0x00,0x40,0x04,0x00,0x00,0x10,0x00,0x10,0x00,0x00,0x10,0x01,0x00,0x00,0x04,0x00,0x04,0x00,0x00,0x44,0x00,0x00,0x1e,0xf1,0xf1,0x01,0x00,0xf0,0x11,0xff,0x1f,0x00,0x04,0x04,0x00,0x00,0x04,0x00,0x04,0x00,0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x40,0x40,0x00,0x00,0x40,0x00,0x40,0x00,0xf0,0x1f,0x1f,0x1f,0x00,0x10,0xff,0x11,0x01,0x40,0x40,0x40,0x00,0x00,0x04,0x40,0x40,0x00,0x10,0x10,0x10,0x00,0x00,0x01,0x10,0x10,0x00,0x04,0x04,0x04,0x00,0x40,0x00,0x04,0x04,0x10,0xf1,0xf1,0x11,0x00,0xff,0x1f,0xff,0x01,0x44,0x40,0x00,0x04,0x00,0x00,0x04,0x40,0x00,0x11,0x10,0x00,0x01,0x00,0x00,0x01,0x10,0x40,0x04,0x04,0x40,0x00,0x00,0x40,0x00,0x04,0x1e,0x1f,0xff,0xf1,0x1f,0xf1,0x11,0x1f,0x0f,0x40,0x00,0x40,0x00,0x44,0x44,0x04,0x04,0x00,0x10,0x00,0x10,0x00,0x11,0x11,0x01,0x01,0x00,0x04,0x00,0x04,0x40,0x44,0x44,0x40,0x00,0xf0,0xff,0x1f,0xff,0xf1,0x11,0xf1,0xff,0x01,0x04,0x04,0x00,0x40,0x04,0x04,0x04,0x40,0x00,0x01,0x01,0x00,0x10,0x01,0x01,0x01,0x10,0x40,0x40,0x00,0x00,0x44,0x40,0x40,0x00,0x04,0x10,0xf1,0xff,0xff,0x11,0x1f,0x1f,0x1f,0x01,0x44,0x00,0x00,0x00,0x44,0x40,0x40,0x04,0x00,0x11,0x00,0x00,0x00,0x11,0x10,0x10,0x01,0x40,0x04,0x00,0x00,0x40,0x04,0x04,0x44,0x00,0x10,0xff,0xff,0xff,0x1f,0xf1,0xff,0xf1,0x01,0x40,0x00,0x40,0x00,0x44,0x00,0x40,0x40,0x00,0x10,0x00,0x10,0x00,0x11,0x00,0x10,0x10,0x00,0x04,0x00,0x04,0x40,0x04,0x00,0x04,0x04,0xf0,0xff,0x11,0x11,0x11,0xff,0x1f,0x11,0x01,0x00,0x40,0x44,0x44,0x44,0x00,0x00,0x44,0x00,0x00,0x10,0x11,0x11,0x11,0x00,0x00,0x11,0x00,0x00,0x44,0x44,0x44,0x04,0x00,0x40,0x04,0xfe,0x1f,0x11,0x1f,0x11,0xf1,0xf1,0xff,0x01,0x00,0x00,0x04,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x01,0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x40,0x00,0x04,0x04,0x04,0x00,0x00,0x00,0x00,0x10,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_14_B[613] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x08,0x00,0x00,0x00,0x08,0x00,0x22,0x00,0x02,0x02,0x00,0x00,0x00,0x02,0x80,0x08,0x80,0x80,0x00,0x00,0x00,0x80,0x00,0x20,0x02,0x20,0x20,0x00,0x00,0x00,0x20,0x00,0x88,0x88,0x88,0x88,0xff,0xff,0xf8,0x88,0x00,0x02,0x22,0x20,0x02,0x00,0x20,0x02,0x22,0x80,0x80,0x08,0x88,0x00,0x00,0x88,0x80,0x08,0x20,0x20,0x02,0x22,0x00,0x00,0x22,0x20,0x02,0xf8,0x8f,0x8f,0x8f,0xff,0xff,0x88,0x8f,0x00,0x00,0x02,0x22,0x20,0x20,0x00,0x22,0x20,0x00,0x80,0x80,0x08,0x08,0x08,0x80,0x08,0x08,0x00,0x20,0x20,0x02,0x02,0x02,0x20,0x02,0x02,0xff,0x88,0xf8,0xf8,0x88,0xf8,0x8f,0x8f,0x00,0x20,0x22,0x00,0x20,0x02,0x02,0x02,0x22,0x00,0x88,0x08,0x00,0x88,0x80,0x80,0x80,0x08,0x00,0x22,0x02,0x00,0x22,0x20,0x20,0x20,0x02,0x8f,0x88,0xff,0xff,0xf8,0x8f,0xff,0x88,0x00,0x20,0x02,0x20,0x20,0x22,0x00,0x20,0x22,0x00,0x88,0x00,0x08,0x88,0x08,0x00,0x88,0x08,0x00,0x22,0x00,0x02,0x22,0x02,0x00,0x22,0x02,0xf8,0xf8,0xf8,0x88,0x88,0xf8,0x8f,0xf8,0x00,0x00,0x22,0x02,0x22,0x22,0x00,0x22,0x00,0x00,0x80,0x88,0x80,0x88,0x08,0x80,0x08,0x00,0x00,0x20,0x22,0x20,0x22,0x02,0x20,0x02,0x00,0xf8,0x8f,0x88,0x8f,0x88,0xff,0x88,0xff,0x00,0x00,0x22,0x22,0x02,0x02,0x20,0x02,0x00,0x00,0x80,0x88,0x88,0x80,0x00,0x88,0x00,0x00,0x00,0x20,0x22,0x22,0x20,0x00,0x22,0x00,0x80,0xff,0x88,0x88,0x00,0x00,0x88,0xf8,0xff,0x07,0x00,0x02,0x22,0x00,0x00,0x22,0x20,0x00,0x00,0x80,0x80,0x08,0x00,0x80,0x08,0x08,0x00,0x00,0x20,0x20,0x02,0x00,0x20,0x02,0x02,0x00,0xf8,0x8f,0x8f,0x00,0x00,0xf8,0x8f,0xff,0x00,0x22,0x00,0x00,0x00,0x00,0x20,0x20,0x00,0x80,0x08,0x00,0x00,0x00,0x00,0x08,0x08,0x00,0x20,0x02,0x00,0x00,0x00,0x00,0x02,0x02,0x00,0x88,0x88,0xff,0x00,0x00,0x88,0xf8,0xf8,0x07,0x02,0x02,0x02,0x00,0x00,0x22,0x20,0x02,0x80,0x80,0x80,0x00,0x00,0x80,0x08,0x88,0x00,0x20,0x20,0x20,0x00,0x00,0x20,0x02,0x22,0x00,0xf8,0x8f,0x88,0x00,0x00,0x8f,0x8f,0x88,0x00,0x00,0x22,0x22,0x02,0x00,0x00,0x22,0x20,0x00,0x80,0x88,0x88,0x00,0x00,0x80,0x08,0x08,0x00,0x20,0x22,0x22,0x00,0x00,0x20,0x02,0x82,0xff,0x88,0xf8,0x88,0xf8,0xf8,0x8f,0xff,0x00,0x20,0x22,0x20,0x20,0x20,0x02,0x02,0x02,0x00,0x88,0x08,0x08,0x08,0x88,0x80,0x80,0x00,0x00,0x22,0x02,0x02,0x02,0x22,0x20,0x20,0x00,0x8f,0x88,0x8f,0x8f,0x8f,0x8f,0xf8,0x88,0x00,0x20,0x02,0x02,0x02,0x02,0x22,0x20,0x22,0x00,0x88,0x80,0x80,0x80,0x80,0x08,0x88,0x08,0x00,0x22,0x20,0x20,0x20,0x20,0x02,0x22,0x02,0xf8,0xf8,0xff,0xf8,0xf8,0x88,0x8f,0x88,0x00,0x02,0x02,0x00,0x02,0x22,0x20,0x22,0x22,0x80,0x80,0x00,0x80,0x80,0x08,0x88,0x88,0x08,0x20,0x20,0x00,0x20,0x20,0x02,0x22,0x22,0x02,0x88,0x8f,0xf8,0x8f,0x8f,0xff,0x88,0x88,0x00,0x22,0x20,0x00,0x02,0x00,0x02,0x22,0x20,0x80,0x08,0x08,0x80,0x00,0x80,0x80,0x08,0x08,0x20,0x02,0x02,0x20,0x00,0x20,0x20,0x02,0x02,0x88,0xf8,0xf8,0xf8,0xff,0x88,0x8f,0xff,0x00,0x20,0x02,0x22,0x00,0x20,0x20,0x20,0x00,0x00,0x88,0x80,0x08,0x00,0x08,0x08,0x08,0x00,0x00,0x22,0x20,0x02,0x00,0x02,0x02,0x02,0x00,0xf8,0x88,0x8f,0x8f,0xff,0x8f,0xf8,0xf8,0x07,0x00,0x02,0x00,0x02,0x00,0x20,0x00,0x00,0x00,0x80,0x00,0x80,0x00,0x00,0x08,0x00,0x00,0x00,0x20,0x00,0x20,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_16_B[604] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x20,0xfe,0xe3,0xe3,0xe3,0xe3,0xff,0xe3,0x03,0x44,0x40,0x40,0x04,0x04,0x04,0x40,0x44,0x80,0x08,0x08,0x88,0x80,0x80,0x00,0x88,0x08,0x10,0x01,0x01,0x11,0x10,0x10,0x00,0x11,0x01,0x22,0xe2,0xe3,0xe3,0xe3,0x3f,0x3e,0x22,0x40,0x44,0x40,0x40,0x00,0x00,0x40,0x40,0x00,0x88,0x08,0x08,0x08,0x00,0x00,0x08,0x08,0x00,0x11,0x01,0x01,0x01,0x00,0x00,0x01,0x01,0x20,0xfe,0xe3,0xe3,0xff,0x3f,0x3e,0x3e,0x1e,0x04,0x40,0x40,0x00,0x40,0x40,0x40,0x40,0x80,0x00,0x08,0x08,0x00,0x08,0x08,0x08,0x08,0x10,0x00,0x01,0x01,0x00,0x01,0x01,0x01,0x01,0x3e,0x22,0xe2,0xff,0xe3,0x23,0x3e,0x3e,0x00,0x44,0x44,0x00,0x40,0x40,0x44,0x04,0x04,0x80,0x88,0x08,0x00,0x08,0x88,0x88,0x80,0x00,0x10,0x11,0x01,0x00,0x01,0x11,0x11,0x10,0x3c,0x3e,0xfe,0xe3,0xe3,0xe3,0x23,0x3e,0x02,0x44,0x40,0x40,0x40,0x40,0x40,0x04,0x44,0x80,0x08,0x08,0x08,0x08,0x08,0x88,0x80,0x08,0x10,0x01,0x01,0x01,0x01,0x01,0x11,0x10,0x01,0x22,0x3e,0xe2,0xe3,0xe3,0x23,0xe2,0x23,0x40,0x04,0x40,0x40,0x40,0x00,0x44,0x00,0x04,0x88,0x00,0x08,0x08,0x08,0x80,0x08,0x80,0x00,0x11,0x00,0x01,0x01,0x01,0x10,0x01,0x10,0x20,0xe2,0xff,0x23,0x20,0xe0,0x23,0xfe,0x03,0x44,0x04,0x04,0x00,0x00,0x00,0x44,0x00,0x80,0x88,0x80,0x00,0x00,0x00,0x80,0x08,0x00,0x10,0x11,0x10,0x00,0x00,0x00,0x10,0x01,0x00,0x22,0x3e,0xe2,0x03,0xe0,0xff,0x23,0x3e,0x40,0x04,0x40,0x00,0x00,0x40,0x40,0x04,0x04,0x88,0x00,0x08,0x00,0x00,0x08,0x88,0x80,0x00,0x11,0x00,0x01,0x00,0x00,0x01,0x11,0x10,0x20,0xfe,0xe3,0x03,0x00,0x20,0x22,0x3e,0x02,0x00,0x40,0x44,0x00,0x00,0x40,0x04,0x40,0x00,0x00,0x88,0x08,0x00,0x00,0x88,0x00,0x08,0x00,0x00,0x11,0x01,0x00,0x00,0x11,0x00,0x01,0x3e,0x3e,0x22,0x02,0xe0,0x3f,0xfe,0x3f,0x40,0x04,0x04,0x44,0x00,0x00,0x00,0x00,0x00,0x88,0x80,0x80,0x08,0x00,0x00,0x00,0x00,0x00,0x11,0x10,0x10,0x01,0x00,0x00,0x00,0x00,0x20,0x22,0x3e,0x22,0x3e,0xfe,0x3f,0xfe,0x03,0x04,0x04,0x40,0x04,0x04,0x44,0x40,0x00,0x80,0x80,0x00,0x88,0x80,0x80,0x08,0x08,0x00,0x10,0x10,0x00,0x11,0x10,0x10,0x01,0x01,0x00,0x3e,0xfe,0x3f,0xfe,0x23,0x22,0x3e,0xfe,0x01,0x04,0x40,0x44,0x40,0x44,0x04,0x04,0x04,0x80,0x00,0x88,0x08,0x88,0x88,0x80,0x80,0x00,0x10,0x00,0x11,0x01,0x11,0x11,0x10,0x10,0x3c,0xfe,0x23,0x22,0xe2,0x23,0x3e,0x3e,0x02,0x04,0x40,0x00,0x40,0x04,0x04,0x00,0x44,0x80,0x00,0x08,0x00,0x88,0x80,0x00,0x80,0x08,0x10,0x00,0x01,0x00,0x11,0x10,0x00,0x10,0x01,0xfe,0x23,0xfe,0x23,0xe2,0xe3,0xff,0x23,0x40,0x00,0x44,0x40,0x44,0x04,0x00,0x04,0x04,0x08,0x80,0x08,0x88,0x88,0x00,0x80,0x80,0x00,0x01,0x10,0x01,0x11,0x11,0x00,0x10,0x10,0x20,0xfe,0x3f,0xe2,0x23,0x3e,0xfe,0xe3,0x03,0x44,0x00,0x44,0x04,0x04,0x44,0x40,0x04,0x80,0x08,0x80,0x88,0x80,0x80,0x08,0x88,0x00,0x10,0x01,0x10,0x11,0x10,0x10,0x01,0x11,0x00,0xe2,0x23,0x22,0xe2,0x23,0x3e,0x22,0x22,0x00,0x00,0x04,0x04,0x04,0x04,0x40,0x00,0x04,0x00,0x80,0x80,0x80,0x80,0x00,0x08,0x80,0x00,0x00,0x10,0x10,0x10,0x10,0x00,0x01,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_18_L[613] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x20,0x00,0x00,0x00,0x80,0xff,0x88,0x8f,0xff,0x88,0xff,0x8f,0x8f,0x00,0x20,0x02,0x02,0x20,0x00,0x00,0x22,0x20,0x00,0x88,0x80,0x00,0x08,0x00,0x80,0x08,0x08,0x00,0x22,0x20,0x00,0x02,0x00,0x20,0x02,0x02,0xf8,0xf8,0xf8,0x8f,0xf8,0xff,0x8f,0xf8,0x00,0x02,0x02,0x22,0x20,0x00,0x00,0x22,0x20,0x80,0x80,0x80,0x08,0x08,0x00,0x80,0x08,0x08,0x20,0x20,0x20,0x02,0x02,0x00,0x20,0x02,0x02,0x88,0x8f,0x8f,0xf8,0xf8,0xf8,0xff,0x8f,0x00,0x20,0x20,0x20,0x20,0x20,0x02,0x00,0x20,0x00,0x08,0x08,0x08,0x08,0x88,0x00,0x00,0x08,0x00,0x02,0x02,0x02,0x02,0x22,0x00,0x00,0x02,0xf8,0x8f,0xf8,0x8f,0x8f,0x8f,0xff,0xff,0x00,0x02,0x20,0x00,0x00,0x20,0x20,0x00,0x02,0x80,0x00,0x08,0x00,0x00,0x08,0x08,0x80,0x00,0x20,0x00,0x02,0x00,0x00,0x02,0x02,0x20,0x00,0xf8,0xf8,0xf8,0x8f,0xff,0xf8,0x8f,0x88,0x07,0x20,0x02,0x02,0x22,0x20,0x02,0x20,0x02,0x00,0x88,0x80,0x80,0x08,0x88,0x00,0x88,0x00,0x00,0x22,0x20,0x20,0x02,0x22,0x00,0x22,0x00,0x8f,0xf8,0x8f,0xff,0x88,0x88,0xff,0x8f,0x00,0x20,0x02,0x22,0x00,0x02,0x20,0x00,0x22,0x00,0x88,0x80,0x08,0x80,0x00,0x08,0x80,0x08,0x00,0x22,0x20,0x02,0x20,0x00,0x02,0x20,0x02,0xf8,0x88,0x88,0x00,0x80,0xff,0xf8,0xf8,0x00,0x02,0x20,0x22,0x00,0x00,0x02,0x22,0x02,0x80,0x00,0x88,0x08,0x00,0x80,0x80,0x88,0x00,0x20,0x00,0x22,0x02,0x00,0x20,0x20,0x22,0x00,0xf8,0xff,0x88,0x00,0x00,0x88,0x8f,0x88,0x07,0x02,0x00,0x20,0x00,0x00,0x02,0x02,0x02,0x80,0x00,0x00,0x08,0x00,0x80,0x80,0x80,0x00,0x20,0x00,0x00,0x02,0x00,0x20,0x20,0x20,0x00,0x88,0x8f,0xf8,0x00,0x00,0xf8,0x88,0xff,0x00,0x20,0x20,0x02,0x00,0x00,0x20,0x02,0x00,0x00,0x08,0x88,0x00,0x00,0x00,0x88,0x00,0x00,0x00,0x02,0x22,0x00,0x00,0x00,0x22,0x00,0x80,0xff,0xf8,0xf8,0x07,0x00,0x8f,0xf8,0xff,0x00,0x20,0x20,0x20,0x00,0x00,0x02,0x00,0x20,0x00,0x08,0x08,0x08,0x00,0x80,0x00,0x00,0x08,0x00,0x02,0x02,0x02,0x00,0x20,0x00,0x00,0x02,0xf8,0x8f,0x8f,0x88,0xff,0x88,0xff,0x8f,0x00,0x00,0x22,0x00,0x02,0x02,0x20,0x20,0x20,0x00,0x80,0x08,0x80,0x80,0x00,0x08,0x08,0x08,0x00,0x20,0x02,0x20,0x20,0x00,0x02,0x02,0x02,0x88,0x8f,0xff,0x8f,0xf8,0x8f,0xf8,0xf8,0x00,0x02,0x02,0x00,0x20,0x20,0x22,0x02,0x02,0x80,0x80,0x00,0x00,0x08,0x88,0x88,0x80,0x00,0x20,0x20,0x00,0x00,0x02,0x22,0x22,0x20,0x00,0xf8,0xf8,0xff,0xff,0x8f,0x88,0x88,0x8f,0x07,0x02,0x02,0x00,0x20,0x20,0x00,0x22,0x20,0x80,0x80,0x00,0x00,0x08,0x08,0x80,0x08,0x08,0x20,0x20,0x00,0x00,0x02,0x02,0x20,0x02,0x02,0x88,0xff,0x8f,0x88,0xf8,0xf8,0x8f,0xf8,0x00,0x02,0x02,0x22,0x02,0x02,0x02,0x20,0x02,0x80,0x80,0x80,0x88,0x80,0x80,0x00,0x88,0x00,0x20,0x20,0x20,0x22,0x20,0x20,0x00,0x22,0x00,0xf8,0x88,0xf8,0xf8,0x8f,0x8f,0xff,0xf8,0x00,0x22,0x20,0x00,0x02,0x00,0x02,0x20,0x00,0x80,0x08,0x08,0x80,0x00,0x80,0x00,0x08,0x00,0x20,0x02,0x02,0x20,0x00,0x20,0x00,0x02,0x00,0x88,0xff,0xff,0xff,0xff,0xff,0x8f,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_20_L[613] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x20,0x00,0x00,0x02,0x40,0x00,0x40,0x00,0x00,0x04,0x00,0x40,0x00,0x08,0x00,0x08,0x00,0x80,0x00,0x00,0x08,0x00,0x01,0x00,0x01,0x00,0x10,0x00,0x00,0x01,0x20,0x3e,0x22,0xe2,0x3f,0x3e,0xfe,0x23,0x02,0x40,0x44,0x44,0x04,0x44,0x40,0x40,0x44,0x00,0x88,0x88,0x88,0x80,0x08,0x08,0x88,0x08,0x00,0x11,0x11,0x11,0x10,0x01,0x01,0x11,0xc1,0x3f,0x22,0x22,0x22,0x22,0x3e,0x3e,0x3e,0x00,0x00,0x04,0x04,0x44,0x04,0x04,0x04,0x04,0x00,0x80,0x80,0x80,0x88,0x80,0x80,0x80,0x00,0x00,0x10,0x10,0x10,0x11,0x10,0x10,0x10,0xe0,0xff,0xff,0x3f,0x22,0x3e,0x3e,0x3e,0x02,0x00,0x00,0x04,0x44,0x04,0x04,0x04,0x44,0x00,0x00,0x80,0x80,0x88,0x80,0x80,0x80,0x08,0x00,0x00,0x10,0x10,0x11,0x10,0x10,0x10,0xc1,0xff,0x3f,0x22,0x3e,0x3e,0x3e,0x22,0x22,0x00,0x40,0x40,0x04,0x40,0x04,0x44,0x44,0x00,0x00,0x08,0x88,0x00,0x88,0x80,0x88,0x08,0x00,0x00,0x01,0x11,0x00,0x11,0x10,0x11,0x01,0xe0,0x23,0x3e,0xfe,0x3f,0x3e,0x22,0xe2,0x03,0x04,0x00,0x40,0x00,0x00,0x04,0x44,0x00,0x80,0x00,0x00,0x08,0x00,0x80,0x80,0x08,0x00,0x10,0x00,0x00,0x01,0x00,0x10,0x10,0x01,0x00,0xfe,0xff,0x3f,0xe2,0xff,0xff,0xe3,0x3f,0x00,0x00,0x40,0x40,0x00,0x00,0x04,0x04,0x04,0x00,0x00,0x08,0x08,0x00,0x80,0x80,0x80,0x00,0x00,0x00,0x01,0x01,0x00,0x10,0x10,0x10,0xfc,0x3f,0x3e,0x22,0x00,0x3e,0xe2,0x23,0x02,0x00,0x40,0x40,0x00,0x00,0x04,0x04,0x04,0x00,0x00,0x08,0x08,0x00,0x80,0x80,0x80,0x00,0x00,0x00,0x01,0x01,0x00,0x10,0x10,0x10,0x00,0xfe,0x3f,0x3e,0x00,0x00,0xfe,0x3f,0x3e,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x44,0x04,0x08,0x08,0x08,0x00,0x00,0x00,0x80,0x88,0x00,0x01,0x01,0x01,0x00,0x00,0x00,0x10,0x11,0x20,0x22,0x3e,0x02,0x00,0xe0,0x3f,0x22,0x02,0x44,0x40,0x40,0x00,0x00,0x40,0x40,0x04,0x80,0x08,0x08,0x08,0x00,0x00,0x08,0x88,0x00,0x10,0x01,0x01,0x01,0x00,0x00,0x01,0x11,0x00,0xe2,0x3f,0xfe,0x01,0xe0,0x23,0x3e,0xfe,0x41,0x04,0x04,0x00,0x00,0x00,0x44,0x00,0x00,0x88,0x80,0x00,0x00,0x00,0x80,0x08,0x00,0x00,0x11,0x10,0x00,0x00,0x00,0x10,0x01,0x00,0x20,0x22,0xe2,0xe3,0xff,0xff,0xe3,0x3f,0x02,0x04,0x44,0x40,0x04,0x00,0x44,0x00,0x44,0x80,0x80,0x08,0x88,0x00,0x80,0x08,0x80,0x08,0x10,0x10,0x01,0x11,0x00,0x10,0x01,0x10,0x01,0xfe,0xe3,0x23,0xe2,0x3f,0xe2,0x3f,0x22,0x00,0x40,0x40,0x04,0x00,0x44,0x04,0x40,0x04,0x00,0x08,0x88,0x00,0x80,0x88,0x00,0x88,0x00,0x00,0x01,0x11,0x00,0x10,0x11,0x00,0x11,0xe0,0xe3,0xe3,0xff,0x3f,0x22,0xe2,0xff,0x03,0x44,0x00,0x04,0x00,0x44,0x44,0x04,0x40,0x80,0x08,0x80,0x00,0x80,0x88,0x88,0x00,0x08,0x10,0x01,0x10,0x00,0x10,0x11,0x11,0x00,0x01,0xe2,0x3f,0xe2,0x3f,0x3e,0x22,0xe2,0x23,0x00,0x04,0x40,0x00,0x44,0x40,0x04,0x44,0x00,0x80,0x00,0x08,0x80,0x08,0x88,0x80,0x08,0x00,0x10,0x00,0x01,0x10,0x01,0x11,0x10,0x01,0x3c,0xe2,0xe3,0x3f,0x22,0x3e,0xfe,0xe3,0x03,0x40,0x40,0x04,0x40,0x40,0x00,0x40,0x00,0x00,0x08,0x88,0x00,0x08,0x08,0x00,0x08,0x00,0x00,0x01,0x11,0x00,0x01,0x01,0x00,0x01,0x00,0xfe,0x23,0xe2,0xff,0x3f,0x3e,0x3e,0xfe,0x01,0x00,0x04,0x04,0x00,0x00,0x04,0x00,0x00,0x00,0x80,0x80,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::uint8_t kMazeBits_21_L[604] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x04,0x00,0xfe,0x1f,0xf1,0x11,0xf1,0xf1,0x1f,0xf1,0x01,0x00,0x40,0x40,0x04,0x44,0x40,0x40,0x44,0x00,0x00,0x10,0x10,0x01,0x11,0x10,0x10,0x11,0x00,0x00,0x04,0x44,0x40,0x04,0x04,0x44,0x04,0xf0,0xff,0xff,0xf1,0x11,0x1f,0xff,0x11,0x01,0x04,0x00,0x04,0x04,0x04,0x04,0x04,0x40,0x00,0x01,0x00,0x01,0x01,0x01,0x01,0x01,0x10,0x40,0x00,0x40,0x40,0x40,0x40,0x40,0x00,0x04,0x10,0xff,0x11,0x1f,0xff,0xf1,0x11,0xff,0x01,0x04,0x04,0x40,0x40,0x40,0x40,0x40,0x40,0x00,0x01,0x01,0x10,0x10,0x10,0x10,0x10,0x10,0x40,0x40,0x00,0x04,0x04,0x04,0x04,0x04,0x04,0xf0,0xf1,0xff,0xf1,0x11,0x11,0xff,0x11,0x01,0x40,0x00,0x40,0x04,0x04,0x44,0x00,0x04,0x00,0x10,0x00,0x10,0x01,0x01,0x11,0x00,0x01,0x00,0x04,0x00,0x44,0x40,0x40,0x04,0x40,0x00,0x1e,0xff,0xf1,0xf1,0xf1,0x1f,0x1f,0xf1,0x01,0x04,0x40,0x44,0x00,0x04,0x44,0x40,0x44,0x00,0x01,0x10,0x11,0x00,0x01,0x11,0x10,0x11,0x40,0x00,0x44,0x04,0x40,0x40,0x04,0x44,0x04,0xf0,0x1f,0x1f,0xff,0x1f,0x1f,0xf1,0x11,0x01,0x40,0x40,0x40,0x00,0x40,0x40,0x04,0x04,0x00,0x10,0x10,0x10,0x00,0x10,0x10,0x01,0x01,0x00,0x04,0x04,0x04,0x00,0x04,0x44,0x40,0x00,0xf0,0xf1,0xf1,0x01,0x00,0xf0,0x11,0x1f,0x0f,0x40,0x40,0x00,0x00,0x00,0x00,0x44,0x04,0x00,0x10,0x10,0x00,0x00,0x00,0x00,0x11,0x01,0x00,0x04,0x04,0x00,0x00,0x00,0x40,0x44,0x00,0x10,0x1f,0x1f,0x0f,0x00,0xfe,0x11,0xf1,0x01,0x04,0x04,0x04,0x00,0x00,0x00,0x44,0x04,0x00,0x01,0x01,0x01,0x00,0x00,0x00,0x11,0x01,0x40,0x40,0x40,0x00,0x00,0x00,0x40,0x44,0x00,0xfe,0xf1,0x11,0x0f,0x00,0xf0,0x1f,0x11,0x0f,0x00,0x04,0x04,0x00,0x00,0x04,0x44,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x01,0x11,0x00,0x00,0x40,0x40,0x00,0x00,0x40,0x40,0x04,0x00,0xf0,0x1f,0xff,0x01,0x00,0x10,0x1f,0xff,0x01,0x00,0x40,0x40,0x04,0x40,0x40,0x40,0x40,0x00,0x00,0x10,0x10,0x01,0x10,0x10,0x10,0x10,0x00,0x00,0x04,0x44,0x00,0x04,0x04,0x04,0x04,0x10,0xff,0x1f,0x11,0xff,0x1f,0xf1,0x11,0x01,0x44,0x00,0x40,0x04,0x00,0x40,0x00,0x44,0x00,0x11,0x00,0x10,0x01,0x00,0x10,0x00,0x11,0x40,0x04,0x00,0x44,0x00,0x00,0x04,0x40,0x04,0x10,0x11,0xff,0xf1,0xff,0xff,0xff,0x11,0x1f,0x04,0x44,0x40,0x00,0x00,0x00,0x40,0x04,0x00,0x01,0x11,0x10,0x00,0x00,0x00,0x10,0x01,0x40,0x40,0x04,0x04,0x00,0x00,0x00,0x44,0x00,0xf0,0x1f,0x11,0xf1,0xff,0xff,0x1f,0x1f,0x01,0x04,0x40,0x04,0x00,0x40,0x00,0x04,0x44,0x00,0x01,0x10,0x01,0x00,0x10,0x00,0x01,0x11,0x40,0x00,0x44,0x00,0x00,0x04,0x40,0x40,0x04,0x10,0xff,0xf1,0x1f,0x1f,0x1f,0xf1,0xf1,0x01,0x40,0x40,0x04,0x04,0x04,0x40,0x40,0x00,0x00,0x10,0x10,0x01,0x01,0x01,0x10,0x10,0x00,0x00,0x04,0x44,0x40,0x40,0x00,0x04,0x04,0x00,0xf0,0xf1,0xf1,0xf1,0xf1,0x1f,0xff,0xff,0x01,0x00,0x04,0x04,0x44,0x40,0x40,0x40,0x40,0x00,0x00,0x01,0x01,0x11,0x10,0x10,0x10,0x10,0x00,0x40,0x40,0x40,0x04,0x04,0x04,0x04,0x04,0xf0,0x1f,0x1f,0x11,0x1f,0xff,0x11,0x11,0x01,0x00,0x00,0x40,0x00,0x04,0x00,0x04,0x04,0x00,0x00,0x00,0x10,0x00,0x01,0x00,0x01,0x01,0x00,0x00,0x00,0x04,0x40,0x00,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static constexpr std::array<MazePresetGrid,11> kMazePresetGrids{{
    {"MazeCreate_S_03_R", 71, 67, -17.325474f, -17.325472f, kMazeBits_03_R},
    {"MazeCreate_S_06_R", 71, 71, -17.325474f, -17.325470f, kMazeBits_06_R},
    {"MazeCreate_S_09_T", 71, 71, -17.325474f, -17.325472f, kMazeBits_09_T},
    {"MazeCreate_S_10_T", 71, 71, -17.325474f, -17.325472f, kMazeBits_10_T},
    {"MazeCreate_S_11_T", 71, 71, -17.325474f, -17.325474f, kMazeBits_11_T},
    {"MazeCreate_S_12_T", 70, 70, -17.047333f, -17.177109f, kMazeBits_12_T},
    {"MazeCreate_S_14_B", 70, 70, -17.325472f, -17.325466f, kMazeBits_14_B},
    {"MazeCreate_S_16_B", 69, 70, -16.821472f, -17.325466f, kMazeBits_16_B},
    {"MazeCreate_S_18_L", 70, 70, -17.325472f, -17.325470f, kMazeBits_18_L},
    {"MazeCreate_S_20_L", 69, 71, -16.987530f, -17.325466f, kMazeBits_20_L},
    {"MazeCreate_S_21_L", 70, 69, -17.122684f, -17.081310f, kMazeBits_21_L},
}};

} // namespace

bool runtimeRequested(const Model&m){return m.enabled[Esp]||m.enabled[Line]||m.enabled[SpamJump]||m.enabled[AutoReady]||m.enabled[AutoHammer];}

struct HoleStampRef { std::uint8_t stage; char variant; float dx; float dz; };
static constexpr HoleStampRef kHoleStampRefs[156] = {
    {0, 0, 0.f, 0.f},
    {1, 'a', -0.004938f, -13.836334f},
    {1, 'a', 5.989526f, -3.453961f},
    {1, 'a', -5.999442f, -3.453963f},
    {1, 'a', -0.004939f, 0.006829f},
    {1, 'a', 11.984034f, 3.467615f},
    {1, 'a', -8.996641f, 5.198079f},
    {1, 'a', -5.999444f, 10.389197f},
    {1, 'b', 2.992335f, -12.105864f},
    {1, 'b', -8.996642f, -8.645081f},
    {1, 'b', -3.002131f, -8.645080f},
    {1, 'b', 5.989528f, -6.914756f},
    {1, 'b', -8.996643f, -1.723495f},
    {1, 'b', 5.989527f, 0.006830f},
    {1, 'b', -11.993914f, 6.928399f},
    {1, 'c', 2.992337f, -12.105863f},
    {1, 'c', -5.999443f, -6.914758f},
    {1, 'c', 2.992335f, -1.723492f},
    {1, 'c', -11.993913f, 0.006827f},
    {1, 'c', 8.986840f, 1.737300f},
    {1, 'c', -3.002131f, 5.198080f},
    {1, 'c', 5.989525f, 6.928404f},
    {1, 'd', 2.992338f, -12.105861f},
    {1, 'd', -3.002129f, -8.645080f},
    {1, 'd', -8.996639f, -8.645082f},
    {1, 'd', 11.984032f, 3.467616f},
    {1, 'd', -3.002132f, 1.737297f},
    {1, 'd', -8.996642f, 5.198078f},
    {1, 'd', 8.986839f, 8.658874f},
    {1, 'e', 8.986843f, -8.645078f},
    {1, 'e', -3.002128f, -8.645079f},
    {1, 'e', -11.993912f, -6.914759f},
    {1, 'e', 5.989527f, -3.453960f},
    {1, 'e', -8.996641f, -1.723496f},
    {1, 'e', 8.986838f, 8.658875f},
    {1, 'e', -8.996644f, 8.658871f},
    {2, 'a', -3.002131f, -12.105864f},
    {2, 'a', 8.986844f, -5.184287f},
    {2, 'a', -0.004938f, -3.453962f},
    {2, 'a', -11.993913f, -3.453965f},
    {2, 'a', 2.992332f, 8.658874f},
    {2, 'a', -5.999444f, 6.928401f},
    {2, 'a', -0.004940f, 13.849989f},
    {2, 'b', -3.002129f, -12.105864f},
    {2, 'b', -8.996643f, -5.184289f},
    {2, 'b', -5.999443f, 0.006827f},
    {2, 'b', 5.989526f, 3.467614f},
    {2, 'b', -11.993914f, 3.467611f},
    {2, 'b', 2.992332f, 8.658873f},
    {2, 'b', -5.999445f, 6.928400f},
    {2, 'c', 5.989528f, -10.375547f},
    {2, 'c', -0.004937f, -6.914757f},
    {2, 'c', 11.984035f, -3.453960f},
    {2, 'c', -8.996642f, -5.184290f},
    {2, 'c', -0.004939f, 6.928402f},
    {2, 'c', -11.993914f, 3.467609f},
    {2, 'c', -0.004941f, 13.849989f},
    {2, 'd', 5.989529f, -10.375545f},
    {2, 'd', 8.986843f, -5.184285f},
    {2, 'd', -0.004939f, 3.467613f},
    {2, 'd', -11.993913f, 3.467608f},
    {2, 'd', 11.984032f, 6.928405f},
    {2, 'd', -0.004940f, 10.389201f},
    {2, 'd', -5.999444f, 6.928399f},
    {2, 'e', -5.999441f, -10.375550f},
    {2, 'e', 2.992336f, -8.645078f},
    {2, 'e', 8.986843f, -5.184283f},
    {2, 'e', -0.004938f, -3.453962f},
    {2, 'e', -11.993911f, -3.453966f},
    {2, 'e', -11.993914f, 3.467607f},
    {2, 'e', -5.999445f, 6.928397f},
    {3, 'a', 2.992339f, -12.105860f},
    {3, 'a', 2.992334f, -1.723492f},
    {3, 'a', 8.986839f, 1.737301f},
    {3, 'a', 5.989525f, 10.389199f},
    {3, 'a', -8.996645f, 8.658869f},
    {3, 'b', -0.004936f, -13.836332f},
    {3, 'b', 5.989529f, -6.914754f},
    {3, 'b', 11.984031f, 3.467616f},
    {3, 'b', -5.999444f, 3.467611f},
    {3, 'b', 2.992331f, 12.119667f},
    {3, 'c', 5.989528f, -6.914753f},
    {3, 'c', -8.996637f, -8.645082f},
    {3, 'c', 8.986838f, 1.737302f},
    {3, 'c', 2.992330f, 12.119668f},
    {3, 'c', -3.002134f, 12.119667f},
    {3, 'd', -0.004937f, -10.375548f},
    {3, 'd', -3.002132f, -1.723493f},
    {3, 'd', 2.992334f, 5.198082f},
    {3, 'd', -3.002133f, 8.658872f},
    {3, 'd', -11.993914f, 6.928398f},
    {3, 'e', -0.004936f, -10.375548f},
    {3, 'e', 8.986843f, -8.645076f},
    {3, 'e', -5.999441f, -3.453964f},
    {3, 'e', -8.996644f, 1.737296f},
    {3, 'e', -8.996645f, 8.658867f},
    {4, 'a', -5.999441f, -10.375550f},
    {4, 'a', 2.992336f, -8.645078f},
    {4, 'a', -8.996643f, -5.184290f},
    {4, 'a', 8.986842f, -1.723491f},
    {4, 'a', -11.993914f, 3.467611f},
    {4, 'b', -3.002131f, -12.105864f},
    {4, 'b', 2.992335f, -5.184287f},
    {4, 'b', 11.984034f, 0.006833f},
    {4, 'b', -8.996640f, 5.198076f},
    {4, 'b', -0.004940f, 10.389201f},
    {4, 'c', -5.999439f, -10.375550f},
    {4, 'c', 8.986844f, -5.184287f},
    {4, 'c', -5.999443f, 0.006827f},
    {4, 'c', 8.986841f, 5.198083f},
    {4, 'c', 2.992332f, 8.658874f},
    {4, 'd', 2.992336f, -8.645077f},
    {4, 'd', -8.996642f, -5.184290f},
    {4, 'd', 8.986841f, -1.723490f},
    {4, 'd', 11.984032f, 6.928404f},
    {4, 'd', 2.992332f, 8.658873f},
    {4, 'e', -8.996641f, -5.184291f},
    {4, 'e', 8.986841f, -1.723489f},
    {4, 'e', -3.002131f, 1.737297f},
    {4, 'e', 2.992330f, 8.658873f},
    {4, 'e', -8.996641f, 5.198074f},
    {5, 'a', 5.989529f, -6.914754f},
    {5, 'a', -3.002132f, -1.723494f},
    {5, 'a', -11.993913f, -3.453965f},
    {5, 'b', -8.996644f, 1.737295f},
    {5, 'b', 2.992334f, 5.198083f},
    {5, 'b', -5.999445f, 10.389196f},
    {5, 'c', -3.002130f, -5.184288f},
    {5, 'c', -11.993911f, -3.453966f},
    {5, 'c', -5.999446f, 10.389194f},
    {5, 'd', 8.986843f, -8.645076f},
    {5, 'd', 5.989523f, 6.928404f},
    {5, 'd', -8.996645f, 8.658869f},
    {5, 'e', 8.986843f, -8.645073f},
    {5, 'e', -11.993910f, -3.453966f},
    {5, 'e', -5.999444f, 6.928401f},
    {6, 'a', -8.996637f, -8.645081f},
    {6, 'a', -5.999441f, -3.453964f},
    {6, 'a', 11.984032f, 6.928405f},
    {6, 'b', -0.004937f, -10.375547f},
    {6, 'b', 8.986844f, -5.184286f},
    {6, 'b', 11.984031f, 3.467616f},
    {6, 'c', -11.993914f, 3.467611f},
    {6, 'c', 11.984031f, 6.928406f},
    {6, 'c', -0.004940f, 10.389200f},
    {6, 'd', 2.992336f, -8.645079f},
    {6, 'd', -8.996640f, -1.723498f},
    {6, 'd', 8.986835f, 8.658874f},
    {6, 'e', -0.004939f, 0.006829f},
    {6, 'e', 8.986841f, 5.198084f},
    {6, 'e', -0.004941f, 10.389199f},
    {7, 'a', 11.984034f, 0.006833f},
    {7, 'b', 2.992332f, 8.658873f},
    {7, 'c', -11.993914f, 6.928398f},
    {7, 'd', 2.992332f, 8.658872f},
    {7, 'e', 8.986842f, -1.723491f},
};
static int parseHoleStampId(const std::string& nm) {
    auto pos = nm.rfind("_Stamp_");
    if (pos == std::string::npos) return 0;
    pos += 7;
    if (pos >= nm.size()) return 0;
    int val = 0;
    for (std::size_t i = pos; i < nm.size(); ++i) {
        if (nm[i] < '0' || nm[i] > '9') return 0;
        val = val * 10 + (nm[i] - '0');
    }
    return (val >= 1 && val <= 155) ? val : 0;
}

struct GameBackend::State {
    API a;void* ownThread=nullptr;
    std::map<std::string,Klass> classes;
    std::map<std::tuple<Klass,std::string,int>,Method> methods;
    std::map<std::pair<Klass,std::string>,Field> fields;
    Root actorRoot,controlRoot,miniGameRoot;
    Root simpleDialogRoot,partyDialogRoot,uiSystemRoot;
    Root joyStickDialogRoot,jumpBtnTrRoot;
    double joyStickScanTime=-1;
    std::string actorUID;
    double nextJump=0,lastJump=-1,lastTick=-1;
    double settleUntil=0,retryAfter=0;
    bool idle=true,needsSettle=false;
    bool simpleLatched=false,partyLatched=false;
    double readyScanTime=-1;
    double simpleEligibleSince=-1,partyEligibleSince=-1,nextReadyClickTime=0;
    std::string hammerWindowKey;
    double hammerFightTime=-1;
    bool hammerLatched=false;
    std::uint64_t jumpRequests=0,readyRequests=0,hammerRequests=0;
    std::string readySummary,hammerSummary;
    std::vector<EspItem> points;
    std::vector<RouteSegment> routeSegments;
    std::vector<WorldMarker> worldMarkers;
    std::vector<RouteNode> routeNodes;
    int currentRouteStage=0;
    double espScanTime=-1;
    std::string status="Waiting for IL2CPP",controllerName,currentMapName,espSummary;
    int currentMapId=0;

    // ---- anti-detection (Stealth stubs + code detours + live object scrub) ----
    Stealth stealth;
    double nextAntiCheatSanitize=0.;
    int sanitizeHits=0;
    // "vng" / "global" / "" until the probe runs. See sanitizeLiveAntiCheat.
    std::string clientFlavor;

    std::string cachedRoundKey;
    struct HoleTileRec {
        Root comp;
        Root tr;
        Root boardGo;
        Vec3 cachedPos{};
        int stampId=0;
        int stage=0;
        bool nameChecked=false;
        bool hasCachedPos=false;
    };
    std::vector<HoleTileRec> holeTiles;
    std::map<std::string,Vec3> holeSavedAnchors;
    bool holeStructureReady=false;

    struct RoadVarRec {
        Root go;
        std::string name;
    };
    struct Road120026State {
        std::string key;
        std::vector<RoadVarRec> vars;
        Root activeGo;
        std::string activeName;
        std::vector<WorldMarker> cachedMarkers;
        std::string cachedSummary;
        bool activeSolved=false;
    } road120026;

    struct DoorObjRec {
        Root go;
        Root parentGo;
        DoorChoice choice;
        Vec3 anchor{};
        std::vector<Root> colliders;
    };
    struct Door120006State {
        std::string key;
        std::vector<DoorObjRec> items;
        bool ready=false;
    } doors120006;

    struct BoardTileRec {
        Root comp;
        Root go;
        Root col;
        Root stableGo;
        Root stableCol;
        Vec3 anchor{};
        Vec3 stableAnchor{};
        bool hasAnchor=false;
        bool hasStable=false;
    };
    struct BoardState {
        std::string key;
        std::vector<BoardTileRec> tiles;
        bool ready=false;
    } boardCache;

    Root activeCamRoot;
    Rect cachedViewport{0.f,0.f,1.f,1.f};
    double camCheckTime=-1;

    enum class MazeBoxRole : std::uint8_t {
        Floor=0,
        StartGate=1,
        FinishGate=2,
        GoalFrame=3,
        WallBox=4
    };
    struct MazeMeshCand {
        Root col;
        Root go;
        std::string variant;
    };
    struct MazeBoxCand {
        Root col;
        Root go;
        std::string variant;
        MazeBoxRole role=MazeBoxRole::WallBox;
    };
    struct MazeState {
        std::string key;
        std::string activeVariant;
        std::vector<MazeMeshCand> meshCandidates;
        std::vector<MazeBoxCand> boxCandidates;
        Root activeMeshCol;
        Bounds meshBounds{};
        float meshStep=0.5f;
        int meshW=0;
        int meshH=0;
        float meshX0=0.f;
        float meshZ0=0.f;
        std::vector<float> meshHeights;
        int nextRayIdx=0;
        int wallHitCount=0;
        int raycastBackendMode=0;
        bool hierarchyScanned=false;
        bool gridComplete=false;
        std::string routeSignature;
        Vec3 lastPlanOrigin{1e30f,1e30f,1e30f};
        std::vector<RouteNode> solvedNodes;
        std::vector<std::array<float,4>> solvedObstacles;
        bool routeReady=false;
    } maze;
    int raycast3Mode=0;

    explicit State(Resolver r):a(r){
        if(a.hasNativeLayout){
            if(auto domain=a.domain_get()){
                if(!a.thread_current()&&!ownThread)ownThread=a.thread_attach(domain);
            }
        }
    }
    ~State(){clearCaches();controlRoot.reset();actorRoot.reset();if(ownThread)a.thread_detach(ownThread);}

    // Zeroes a managed int[] payload (the bucket table). The payload of a
    // managed array is one contiguous allocation, so probing the last element
    // proves the whole span is mapped before memset touches it. A length over
    // 1M is treated as corruption and refused, never walked.
    bool zeroIntArray(Obj arr){
        if(!isValidObjectPointer(arr))return false;
        std::int32_t len=0;
        if(!safeReadAt(arr,0x18,len)||len<=0||len>(1<<20))return false;
        const std::size_t bytes=static_cast<std::size_t>(len)*4u;
        std::int32_t probe=0;
        if(!safeReadAt(arr,0x20+bytes-4,probe))return false;
        std::memset(reinterpret_cast<char*>(arr)+0x20,0,bytes);
        return true;
    }

    // Clears a managed HashSet/Dictionary the way Clear() does, by resetting the
    // header instead of re-entering the VM: the sanitizer may run on a thread
    // the game does not own, and a managed call from there has to attach.
    //
    // Layout, identical for both types (dump.cs field ordinals):
    //   object header 0x10 | _buckets 0x10 | _slots/_entries 0x18
    //   _count 0x20 | _freeList 0x24 | _freeCount 0x28 | _version 0x2C
    // _version is left alone on purpose: bumping it makes a live enumerator
    // throw, and an exception in game code is a louder signal than a stale slot.
    bool resetCollectionHeader(Obj coll){
        std::int32_t count=0;
        if(!safeReadAt(coll,0x20,count)||count<=0)return false;
        Obj buckets=nullptr;
        if(safeReadAt(coll,0x10,buckets))zeroIntArray(buckets);
        std::int32_t zero=0;
        std::int32_t noFree=-1;
        const bool c=safeWriteAt(coll,0x20,zero);
        const bool f=safeWriteAt(coll,0x24,noFree);
        const bool g=safeWriteAt(coll,0x28,zero);
        return c&&f&&g;
    }

    bool clearCollectionObject(Obj coll){
        if(!isValidObjectPointer(coll))return false;
        return resetCollectionHeader(coll);
    }

    bool clearCollectionField(Obj owner,std::size_t offset){
        Obj coll=nullptr;
        if(!safeReadAt(owner,offset,coll))return false;
        return clearCollectionObject(coll);
    }

    // Live object scrub. Runs whether or not the method stubs armed: this is
    // what clears a detection that fired before the stubs landed, or one that
    // arrived through a path the managed stubs do not cover.
    //
    // Same code on both 2.32.0 clients. The VNG and Global dumps were diffed
    // type by type and every object below is identical; see the probe in
    // step 1 for the single field that tells the two apart.
    //
    // Offsets are from dump.cs, not guessed:
    //   AntiCheatListener   _dectectHashSet 0x50, CodeHashDic 0x78
    //                       (ReceiveCB 0x38 is left alone: nulling a delegate the
    //                        game may invoke unguarded trades a quiet report for
    //                        a NullReferenceException, and its callee is stubbed)
    //   ModuleSystem        _cheatDetectDic 0x40, _nextCheatDetectT 0x48
    //   SpeedHackScanner    detectionEvent 0x20, detectionAction 0x28, isRunning 0x30
    //   GPrestoEngine       static detected_types 0x00   (static_fields at klass+0xB8)
    //   SpeedHackProofTime  static speedHackDetected 0x01, warningShot 0x1C
    //   GPresto.Protector.SpeedHack  static isPaused 0x03, currentFalsePositives 0x24
    // 100% read-only client flavor detection (VNG vs Global). Never writes memory.
    void probeClientFlavorReadOnly(){
        if(!a.hasNativeLayout||!clientFlavor.empty())return;
        if(Klass modKlass=tryGame("ModuleSystem")){
            journal::mark(journal::Tag::SanitizeStep,10,
                          reinterpret_cast<std::uintptr_t>(modKlass),0,0);
            clientFlavor=tryField(modKlass,"_msgQueue")?"vng":"global";
            journal::mark(journal::Tag::SanitizeStep,11,
                          clientFlavor=="vng"?1u:2u,0,0);
        }
    }

    // Reactive, minimal-footprint sanitizer:
    // - NEVER runs when only passive/read-only features (ESP, Line, AutoReady, AutoHammer) are used.
    // - NEVER modifies CodeHashDic (0x78) or GPresto.Protector.SpeedHack.isPaused (0x03) or
    //   SpeedHackScanner.isRunning (0x30) or ModuleSystem._nextCheatDetectT (0x48) proactively,
    //   because mutating healthy anti-cheat state is itself a detection vector.
    // - Only clears a pending detection entry if count > 0 or a detected flag is actually non-zero.
    void sanitizeLiveAntiCheat(double now){
        if(!a.hasNativeLayout||now<nextAntiCheatSanitize)return;
        nextAntiCheatSanitize=now+(sanitizeHits>0?0.05:0.25);
        int hits=0;
        journal::mark(journal::Tag::Sanitize,0);

        // 1. FrameWork.Instance -> AntiCheatListener (+0x158) and ModuleSystem (+0xE8)
        journal::mark(journal::Tag::SanitizeStep,1);
        if(Klass fwk=tryGame("FrameWork")){
            if(Obj fw=readSingletonInstance(fwk,"FrameWork")){
                Obj acl=nullptr;
                if(safeReadAt(fw,0x158,acl)&&isValidObjectPointer(acl)&&classLabel(acl)=="AntiCheatListener"){
                    // Only clear _dectectHashSet (0x50) if it actually contains an entry (count > 0).
                    // Never clear CodeHashDic (0x78) because CodeHashDic stores legitimate code hashes!
                    if(clearCollectionField(acl,0x50))++hits;
                }
                Obj modSys=nullptr;
                if(safeReadAt(fw,0xE8,modSys)&&isValidObjectPointer(modSys)&&classLabel(modSys)=="ModuleSystem"){
                    if(clientFlavor.empty()){
                        probeClientFlavorReadOnly();
                    }
                    // Only clear _cheatDetectDic (0x40) if it actually queued a report (count > 0).
                    if(clearCollectionField(modSys,0x40))++hits;
                }
            }
        }

        // 2. GPresto.Protector.Engine.GPrestoEngine.detected_types (only if count > 0)
        journal::mark(journal::Tag::SanitizeStep,2);
        if(Klass gpEng=tryKlass(nullptr,"GPresto.Protector.Engine","GPrestoEngine")){
            void* statics=nullptr;
            if(safeReadAt(gpEng,0xB8,statics)&&statics){
                Obj detSet=nullptr;
                if(safeReadAt(statics,0x0,detSet)&&clearCollectionObject(detSet))++hits;
            }
        }

        // 4. SpeedHackProofTime statics: only clear if speedHackDetected (0x1) or warningShot (0x1C) fired
        journal::mark(journal::Tag::SanitizeStep,4);
        if(Klass shptKlass=tryKlass(nullptr,"CodeStage.AntiCheat.Time","SpeedHackProofTime")){
            void* statics=nullptr;
            if(safeReadAt(shptKlass,0xB8,statics)&&statics){
                std::uint8_t flag=0;
                if(safeReadAt(statics,0x1,flag)&&flag!=0){
                    std::uint8_t zero8=0;
                    if(safeWriteAt(statics,0x1,zero8))++hits;
                }
                flag=0;
                if(safeReadAt(statics,0x1C,flag)&&flag!=0){
                    std::uint8_t zero8=0;
                    if(safeWriteAt(statics,0x1C,zero8))++hits;
                }
            }
        }

        // 5. GPresto.Protector.SpeedHack statics: only reset currentFalsePositives (0x24) if > 0.
        //    Do NOT set isPaused = 1 (0x3), so GPresto's normal heartbeat stays intact.
        journal::mark(journal::Tag::SanitizeStep,5);
        if(Klass gpSh=tryKlass(nullptr,"GPresto.Protector","SpeedHack")){
            void* statics=nullptr;
            if(safeReadAt(gpSh,0xB8,statics)&&statics){
                std::uint8_t falsePos=0;
                if(safeReadAt(statics,0x24,falsePos)&&falsePos!=0){
                    std::uint8_t zero8=0;
                    if(safeWriteAt(statics,0x24,zero8))++hits;
                }
            }
        }

        sanitizeHits=hits;
        journal::mark(journal::Tag::SanitizeHit,static_cast<std::uint32_t>(hits));
    }
    void clearCaches(){
        holeTiles.clear();holeSavedAnchors.clear();
        holeStructureReady=false;cachedRoundKey.clear();
        road120026=Road120026State{};
        doors120006=Door120006State{};
        boardCache=BoardState{};
        activeCamRoot.reset();camCheckTime=-1;
        maze=MazeState{};
        miniGameRoot.reset();
        simpleDialogRoot.reset();partyDialogRoot.reset();uiSystemRoot.reset();
        joyStickDialogRoot.reset();jumpBtnTrRoot.reset();joyStickScanTime=-1;
        simpleLatched=false;partyLatched=false;readyScanTime=-1;
        simpleEligibleSince=-1;partyEligibleSince=-1;readySummary.clear();
        hammerWindowKey.clear();hammerFightTime=-1;hammerLatched=false;hammerSummary.clear();
        worldMarkers.clear();routeNodes.clear();
    }

    bool isExecutableMethod(Method m)const{
        if(!m)return false;
        if(a.method_is_generic&&a.method_is_generic(m))return false;
#if defined(__APPLE__)
        if(a.hasNativeLayout){
            void* fnPtr=nullptr;
            void* invPtr=nullptr;
            if(!safeReadAt(m,0,fnPtr)||!safeReadAt(m,sizeof(void*),invPtr)||!fnPtr||!invPtr)return false;
        }
#endif
        return true;
    }

    bool isInstanceField(Field f)const{
        if(!f)return false;
        if(a.field_get_flags){
            int flags=a.field_get_flags(f);
            if((flags&0x0010)!=0||(flags&0x0040)!=0)return false;
        }else if(a.hasNativeLayout){
            const void*ft=a.field_get_type(f);
            std::uint16_t attrs=0;
            if(ft&&safeReadAt(ft,0x08,attrs)&&((attrs&0x0010)!=0||(attrs&0x0040)!=0))return false;
        }
        if(a.field_get_offset){
            std::size_t off=a.field_get_offset(f);
            if(off<0x10||off>0x2000)return false;
        }
        return true;
    }

    Klass tryKlass(const char*image,const char*ns,const char*name){
        std::string key=std::string(image?image:"*")+":"+ns+":"+name;
        if(auto it=classes.find(key);it!=classes.end())return it->second;
        auto domain=a.domain_get();if(!domain)return nullptr;
        if(!a.thread_current()&&!ownThread){ownThread=a.thread_attach(domain);}
        std::size_t count=0;auto list=a.domain_get_assemblies(domain,&count);
        if(!list||count>4096)return nullptr;
        for(std::size_t i=0;i<count;++i){
            auto im=a.assembly_get_image(list[i]);const char*n=a.image_get_name(im);
            if(!n)continue;
            if(!image||!*image||std::strcmp(n,image)==0){
                auto c=a.class_from_name(im,ns,name);
                if(c){classes[key]=c;return c;}
            }
        }
        return nullptr;
    }

    Klass klass(const char*image,const char*ns,const char*name){
        auto domain=a.domain_get();if(!domain)throw std::runtime_error("Waiting for IL2CPP domain");
        if(!a.thread_current()&&!ownThread){ownThread=a.thread_attach(domain);if(!ownThread)throw std::runtime_error("IL2CPP attach failed");}
        std::size_t count=0;auto list=a.domain_get_assemblies(domain,&count);
        if(!list||count>4096)throw std::runtime_error("Waiting for assemblies");
        if(auto c=tryKlass(image,ns,name))return c;
        throw std::runtime_error(std::string("Class unavailable: ")+name);
    }

    Klass tryGame(const char*name){
        if(auto c=tryKlass("Assembly-CSharp.dll","",name))return c;
        if(auto c=tryKlass("Assembly-CSharp.dll","PlayTogether.MiniGame",name))return c;
        if(auto c=tryKlass("Assembly-CSharp.dll","PlayTogether",name))return c;
        return nullptr;
    }
    Klass game(const char*name){
        if(auto c=tryGame(name))return c;
        return klass("Assembly-CSharp.dll","",name);
    }
    Klass unity(const char*name){
        if(auto c=tryKlass("UnityEngine.CoreModule.dll","UnityEngine",name))return c;
        if(auto c=tryKlass("UnityEngine.PhysicsModule.dll","UnityEngine",name))return c;
        if(auto c=tryKlass(nullptr,"UnityEngine",name))return c;
        throw std::runtime_error(std::string("Unity class unavailable: ")+name);
    }

    Method tryMethod(Klass c,const char*name,int argc){
        if(!c)return nullptr;
        auto key=std::make_tuple(c,std::string(name),argc);
        if(auto i=methods.find(key);i!=methods.end())return i->second;
        for(auto k=c;k;k=a.class_get_parent(k)){
            auto m=a.class_get_method_from_name(k,name,argc);
            if(m&&isExecutableMethod(m)){methods[key]=m;return m;}
            if(a.class_get_methods&&a.method_get_name&&a.method_get_param_count){
                void*iter=nullptr;
                while(Method cand=a.class_get_methods(k,&iter)){
                    const char*mn=a.method_get_name(cand);
                    if(mn&&std::strcmp(mn,name)==0&&int(a.method_get_param_count(cand))==argc&&isExecutableMethod(cand)){
                        methods[key]=cand;return cand;
                    }
                }
            }
        }
        return nullptr;
    }

    Method method(Klass c,const char*name,int argc){
        if(auto m=tryMethod(c,name,argc))return m;
        throw std::runtime_error(std::string("Method unavailable: ")+name);
    }

    Field tryField(Klass c,const char*name){
        if(!c)return nullptr;
        auto key=std::make_pair(c,std::string(name));
        if(auto i=fields.find(key);i!=fields.end())return i->second;
        for(auto k=c;k;k=a.class_get_parent(k)){
            auto f=a.class_get_field_from_name(k,name);
            if(f){fields[key]=f;return f;}
        }
        if(a.class_get_fields&&a.field_get_name){
            for(auto k=c;k;k=a.class_get_parent(k)){
                void*iter=nullptr;
                while(Field f=a.class_get_fields(k,&iter)){
                    const char*fn=a.field_get_name(f);
                    if(fn&&iequals(fn,name)){fields[key]=f;return f;}
                }
            }
        }
        return nullptr;
    }

    Field field(Klass c,const char*name){
        if(auto f=tryField(c,name))return f;
        throw std::runtime_error(std::string("Field unavailable: ")+name);
    }

    Field findFieldAny(Klass c,std::initializer_list<const char*>names){
        for(const char*n:names){
            if(auto f=tryField(c,n)){
                if(isInstanceField(f))return f;
            }
        }
        return nullptr;
    }

    bool inheritsFrom(Klass c,const char*targetName){
        for(auto k=c;k;k=a.class_get_parent(k)){
            const char*n=a.class_get_name(k);
            if(n&&std::strcmp(n,targetName)==0)return true;
        }
        return false;
    }

    std::string classLabel(Obj o){
        if(!isValidObjectPointer(o))return {};
        Klass k=a.object_get_class(o);
        if(!k)return {};
        const char*n=a.class_get_name(k);
        return n?std::string(n):std::string();
    }

    Obj invoke(Method m,Obj o,void**args=nullptr){
        if(!m)throw std::runtime_error("Method is not executable");
        Obj ex=nullptr;auto r=a.runtime_invoke(m,o,args,&ex);
        if(ex)throw std::runtime_error("Managed call raised an exception");
        return r;
    }

    Obj call(Obj o,const char*n,int argc=0,void**args=nullptr){
        if(!isValidObjectPointer(o))throw std::runtime_error("Waiting for object");
        return invoke(method(a.object_get_class(o),n,argc),o,args);
    }

    template<class T>T unbox(Obj o){
        if(!o)throw std::runtime_error("Missing boxed return value");
        auto p=a.object_unbox(o);if(!p)throw std::runtime_error("Cannot unbox result");
        T v{};std::memcpy(&v,p,sizeof(v));return v;
    }

    template<class T>T read(Obj o,Field f,int expected=0){
        if(!isValidObjectPointer(o)||!f)throw std::runtime_error("Null field owner");
        if(expected&&a.type_get_type(a.field_get_type(f))!=expected)throw std::runtime_error("Field type changed");
        T v{};a.field_get_value(o,f,&v);return v;
    }

    template<class T>bool writeField(Obj o,Field f,T value){
        if(!isValidObjectPointer(o)||!isInstanceField(f))return false;
        if(a.field_set_value){a.field_set_value(o,f,&value);return true;}
        if(a.field_get_offset){
            std::size_t off=a.field_get_offset(f);
            return safeWriteAt(o,off,value);
        }
        return false;
    }

    template<class T>bool readFieldNamed(Obj o,std::initializer_list<const char*>names,T&out){
        if(!isValidObjectPointer(o))return false;
        Klass k=a.object_get_class(o);
        if(Field f=findFieldAny(k,names)){
            if(a.field_get_offset){
                return safeReadAt(o,a.field_get_offset(f),out);
            }
            alignas(16) unsigned char scratch[64]{};
            a.field_get_value(o,f,scratch);
            std::memcpy(&out,scratch,sizeof(T));
            return true;
        }
        return false;
    }

    Obj readObjFieldNamed(Obj o,std::initializer_list<const char*>names){
        if(!isValidObjectPointer(o))return nullptr;
        Klass k=a.object_get_class(o);
        if(Field f=findFieldAny(k,names)){
            int t=a.type_get_type(a.field_get_type(f));
            if(t==0x12||t==0x15||t==0x1c||t==0x1d){
                Obj val=nullptr;
                if(a.field_get_offset){
                    if(safeReadAt(o,a.field_get_offset(f),val)&&isValidObjectPointer(val))return val;
                    return nullptr;
                }
                alignas(16) unsigned char scratch[64]{};
                a.field_get_value(o,f,scratch);
                std::memcpy(&val,scratch,sizeof(Obj));
                if(isValidObjectPointer(val))return val;
            }
        }
        return nullptr;
    }

    Obj ref(Obj o,const char*n){return read<Obj>(o,field(a.object_get_class(o),n));}

    Obj findRefByClass(Obj o,const char*targetClassName,std::initializer_list<const char*>names){
        if(!isValidObjectPointer(o))return nullptr;
        if(Obj named=readObjFieldNamed(o,names)){
            Klass nk=a.object_get_class(named);
            if(nk&&(classLabel(named)==targetClassName||inheritsFrom(nk,targetClassName)))return named;
        }
        if(a.class_get_fields&&a.field_get_offset){
            Klass k=a.object_get_class(o);
            for(auto cur=k;cur;cur=a.class_get_parent(cur)){
                const char*cn=a.class_get_name(cur);
                if(cn&&(std::strcmp(cn,"MonoBehaviour")==0||std::strcmp(cn,"Component")==0||std::strcmp(cn,"Object")==0))break;
                void*iter=nullptr;
                while(Field f=a.class_get_fields(cur,&iter)){
                    if(!isInstanceField(f))continue;
                    int t=a.type_get_type(a.field_get_type(f));
                    if(t==0x12||t==0x1c){
                        Obj val=nullptr;
                        if(safeReadAt(o,a.field_get_offset(f),val)&&isValidObjectPointer(val)){
                            Klass vk=a.object_get_class(val);
                            if(vk&&(classLabel(val)==targetClassName||inheritsFrom(vk,targetClassName)))return val;
                        }
                    }
                }
            }
        }
        return nullptr;
    }

    bool isUnityObject(Klass c){
        if(!c)return false;
        for(auto k=c;k;k=a.class_get_parent(k)){
            const char*n=a.class_get_name(k);
            if(!n)continue;
            if(std::strcmp(n,"MonoBehaviour")==0||std::strcmp(n,"Component")==0||std::strcmp(n,"GameObject")==0||
               std::strcmp(n,"ScriptableObject")==0||std::strcmp(n,"Behaviour")==0||std::strcmp(n,"Transform")==0||
               std::strcmp(n,"Collider")==0)return true;
            if(std::strcmp(n,"Object")==0){
                const char*ns=a.class_get_namespace?a.class_get_namespace(k):nullptr;
                if(ns&&std::strcmp(ns,"UnityEngine")==0)return true;
                if(ns&&std::strcmp(ns,"System")==0)return false;
            }
        }
        return !a.hasNativeLayout;
    }

    bool alive(Obj o){
        if(!isValidObjectPointer(o))return false;
        Klass k=a.object_get_class(o);
        if(!k)return false;
        if(inheritsFrom(k,"ActorControl")||inheritsFrom(k,"List`1")||inheritsFrom(k,"Dictionary`2"))return false;
        if(!isUnityObject(k))return false;
        if(a.hasNativeLayout){
            void* cachedPtr=nullptr;
            if(!safeReadAt(o,0x10,cachedPtr)||!cachedPtr)return false;
#if defined(__APPLE__)
            void* nativeProbe=nullptr;
            if(!safeReadAt(cachedPtr,0,nativeProbe))return false;
#endif
        }
        void*args[]={o};
        try{
            return unbox<bool>(invoke(method(unity("Object"),"op_Implicit",1),nullptr,args));
        }catch(...){
            return false;
        }
    }

    std::string objectName(Obj o){
        if(!o||!alive(o))return {};
        Method m=tryMethod(unity("Object"),"get_name",0);
        if(!m)return {};
        Obj str=nullptr;
        try{str=invoke(m,o);}catch(...){return {};}
        if(!isValidObjectPointer(str))return {};
        std::int32_t len=0;
        if(!safeReadAt(str,0x10,len)||len<=0||len>256)return {};
        std::string out;out.reserve(std::size_t(len));
        for(int i=0;i<len;++i){
            std::uint16_t ch=0;
            if(!safeReadAt(str,0x14+std::size_t(i)*2,ch))break;
            if(ch<0x80)out.push_back(static_cast<char>(ch));
            else if(ch<0x800){
                out.push_back(static_cast<char>(0xC0|(ch>>6)));
                out.push_back(static_cast<char>(0x80|(ch&0x3F)));
            }else{
                out.push_back(static_cast<char>(0xE0|(ch>>12)));
                out.push_back(static_cast<char>(0x80|((ch>>6)&0x3F)));
                out.push_back(static_cast<char>(0x80|(ch&0x3F)));
            }
        }
        return out;
    }

    Obj componentGO(Obj comp){
        if(!comp||!alive(comp))return nullptr;
        Klass k=a.object_get_class(comp);
        if(inheritsFrom(k,"GameObject"))return comp;
        if(!inheritsFrom(k,"Component"))return nullptr;
        try{return call(comp,"get_gameObject");}catch(...){return nullptr;}
    }

    Obj getTransform(Obj goOrComp){
        if(!goOrComp||!alive(goOrComp))return nullptr;
        Klass k=a.object_get_class(goOrComp);
        if(inheritsFrom(k,"Transform"))return goOrComp;
        if(!inheritsFrom(k,"GameObject")&&!inheritsFrom(k,"Component"))return nullptr;
        try{return call(goOrComp,"get_transform");}catch(...){return nullptr;}
    }

    Obj getParent(Obj tr){
        if(!tr||!alive(tr))return nullptr;
        try{return call(tr,"get_parent");}catch(...){return nullptr;}
    }

    bool activeGO(Obj go){
        if(!go||!alive(go))return false;
        try{
            if(auto m=tryMethod(a.object_get_class(go),"get_activeInHierarchy",0))return unbox<bool>(invoke(m,go));
            if(auto m=tryMethod(a.object_get_class(go),"get_activeSelf",0))return unbox<bool>(invoke(m,go));
        }catch(...){}
        return false;
    }

    bool colliderEnabled(Obj col){
        if(!col||!alive(col))return false;
        try{return unbox<bool>(call(col,"get_enabled"));}catch(...){return false;}
    }

    bool colliderTrigger(Obj col){
        if(!col||!alive(col))return false;
        try{return unbox<bool>(call(col,"get_isTrigger"));}catch(...){return false;}
    }

    bool solidCollider(Obj col){
        if(!col||!alive(col))return false;
        Obj go=componentGO(col);
        return activeGO(go)&&colliderEnabled(col)&&!colliderTrigger(col);
    }

    Bounds boundsOfCollider(Obj col){
        if(!col||!alive(col))throw std::runtime_error("Collider unavailable");
        Bounds b=unbox<Bounds>(call(col,"get_bounds"));
        if(!validBounds(b))throw std::runtime_error("Invalid collider bounds");
        return b;
    }

    int objectLayer(Obj go){
        if(!go||!alive(go))return 0;
        try{
            if(Method m=tryMethod(unity("GameObject"),"get_layer",0)){
                int l=unbox<int>(invoke(m,go));
                if(l>=0&&l<32)return l;
            }
        }catch(...){}
        return 0;
    }

    bool raycastColliderDown(Obj col,float x,float z,const Bounds&b,float floorTop,int backendMode,float&outY){
        if(!col||!alive(col))return false;
        if(std::fabs(x-b.center.x)>b.extents.x+1e-4f||std::fabs(z-b.center.z)>b.extents.z+1e-4f)return false;
        struct Ray { Vec3 origin{}; Vec3 direction{}; };
        struct alignas(8) RaycastHitBuf {
            Vec3 point{};
            Vec3 normal{};
            std::uint32_t faceId=0;
            float distance=0.f;
            float uvx=0.f,uvy=0.f;
            int collider=0;
            std::uint8_t pad[32]{};
        };
        const float wallTop=b.center.y+b.extents.y;
        const float originY=wallTop+0.5f;
        // Exact agent.js startMeshHit range (bounds[4]*2 + 1); floor hits (<= floorTop + 0.2f) are preserved as-is so solveMazeGrid ignores them
        float maxDist=std::max(1.0f,b.extents.y*2.0f+1.0f);
        Ray ray{{x,originY,z},{0.f,-1.f,0.f}};

        Klass colKlass=a.object_get_class(col);
        if(!colKlass)colKlass=unity("Collider");

        // Backend 0: Direct native MethodInfo->methodPointer call (or runtime_invoke) on Collider.Raycast_Injected (RVA 0xA271E1C)
        if(backendMode==0){
            if(a.hasNativeLayout){
                void* cachedPtr=nullptr;
                safeReadAt(col,0x10,cachedPtr);
                if(cachedPtr){
                    if(Method m5=tryMethod(colKlass,"Raycast_Injected",5)){
                        void* fnPtr=nullptr;
                        if(safeReadAt(m5,0,fnPtr)&&fnPtr){
                            bool hasHit=false;
                            RaycastHitBuf hit{};
                            using Fn5=void(*)(void*,Ray*,float,bool*,RaycastHitBuf*,const void*);
                            reinterpret_cast<Fn5>(fnPtr)(cachedPtr,&ray,maxDist,&hasHit,&hit,m5);
                            if(!hasHit||!std::isfinite(hit.point.y))return false;
                            outY=hit.point.y;
                            return true;
                        }
                        bool hasHit=false;
                        RaycastHitBuf hit{};
                        void* args5[]={&cachedPtr,&ray,&maxDist,&hasHit,&hit};
                        try{
                            invoke(m5,nullptr,args5);
                            if(!hasHit||!std::isfinite(hit.point.y))return false;
                            outY=hit.point.y;
                            return true;
                        }catch(...){}
                    }
                    if(Method m4=tryMethod(colKlass,"Raycast_Injected",4)){
                        void* fnPtr=nullptr;
                        if(safeReadAt(m4,0,fnPtr)&&fnPtr){
                            bool hasHit=false;
                            RaycastHitBuf hit{};
                            using Fn4=void(*)(Obj,Ray*,float,bool*,RaycastHitBuf*,const void*);
                            reinterpret_cast<Fn4>(fnPtr)(col,&ray,maxDist,&hasHit,&hit,m4);
                            if(!hasHit||!std::isfinite(hit.point.y))return false;
                            outY=hit.point.y;
                            return true;
                        }
                    }
                }
            }
            if(Method m4=tryMethod(colKlass,"Raycast_Injected",4)){
                bool hasHit=false;
                RaycastHitBuf hit{};
                void* args[]={&ray,&maxDist,&hasHit,&hit};
                try{
                    invoke(m4,col,args);
                    if(!hasHit||!std::isfinite(hit.point.y))return false;
                    outY=hit.point.y;
                    return true;
                }catch(...){}
            }
        }

        // Backend 1: Physics.Linecast(Vector3 start, Vector3 end, int layerMask) (dump.cs RVA 0xA273564 — unique 3-arg value-type overload!)
        if(backendMode==1){
            Klass physKlass=tryKlass("UnityEngine.PhysicsModule.dll","UnityEngine","Physics");
            if(!physKlass)physKlass=tryKlass(nullptr,"UnityEngine","Physics");
            if(physKlass){
                const float endY=(wallTop>floorTop+2.0f)?(floorTop+1.45f):(floorTop+0.35f);
                Vec3 startPt{x,originY,z};
                Vec3 endPt{x,endY,z};
                if(Method mLine3=tryMethod(physKlass,"Linecast",3)){
                    int layerMask=-5; // DefaultRaycastLayers (~IgnoreRaycast)
                    void* args3[]={&startPt,&endPt,&layerMask};
                    try{
                        if(unbox<bool>(invoke(mLine3,nullptr,args3))){
                            outY=wallTop;
                            return true;
                        }
                        return false;
                    }catch(...){}
                }
                if(Method mLine2=tryMethod(physKlass,"Linecast",2)){
                    void* args2[]={&startPt,&endPt};
                    try{
                        if(unbox<bool>(invoke(mLine2,nullptr,args2))){
                            outY=wallTop;
                            return true;
                        }
                        return false;
                    }catch(...){}
                }
            }
        }

        // Backend 2 (or fallback when backendMode==0 in mock tests without Raycast_Injected): Managed 3-arg Collider.Raycast
        if(Method m3=tryMethod(colKlass,"Raycast",3)){
            if(raycast3Mode==0&&a.method_get_param&&a.type_get_type){
                const void* p1=a.method_get_param(m3,1);
                if(p1&&a.type_get_type(p1)==0x0c)raycast3Mode=1;
                else raycast3Mode=2;
            }
            try{
                if(raycast3Mode==1){
                    bool hasHit=false;
                    void* args[]={&ray,&maxDist,&hasHit};
                    Obj ret=invoke(m3,col,args);
                    if(!hasHit||!ret)return false;
                    RaycastHitBuf hit=unbox<RaycastHitBuf>(ret);
                    if(!std::isfinite(hit.point.y))return false;
                    outY=hit.point.y;
                    return true;
                }else if(raycast3Mode==2){
                    RaycastHitBuf hit{};
                    void* args[]={&ray,&hit,&maxDist};
                    Obj ret=invoke(m3,col,args);
                    if(!ret||!unbox<bool>(ret))return false;
                    if(!std::isfinite(hit.point.y))return false;
                    outY=hit.point.y;
                    return true;
                }else{
                    RaycastHitBuf buf1{},buf2{};
                    buf1.point.x=maxDist;
                    buf2.point.x=maxDist;
                    void* args[]={&ray,&buf1,&buf2};
                    Obj ret=invoke(m3,col,args);
                    if(!ret)return false;
                    std::string retCls=classLabel(ret);
                    if(retCls=="RaycastHit"){
                        raycast3Mode=1;
                        bool hasHit=(reinterpret_cast<const std::uint8_t*>(&buf2)[0]!=0);
                        if(!hasHit)return false;
                        RaycastHitBuf hit=unbox<RaycastHitBuf>(ret);
                        if(!std::isfinite(hit.point.y))return false;
                        outY=hit.point.y;
                        return true;
                    }else{
                        raycast3Mode=2;
                        if(!unbox<bool>(ret))return false;
                        if(!std::isfinite(buf1.point.y))return false;
                        outY=buf1.point.y;
                        return true;
                    }
                }
            }catch(...){}
        }
        return false;
    }

    Vec3 worldPos(Obj tr){
        if(!tr||!alive(tr))throw std::runtime_error("Transform unavailable");
        Vec3 v=unbox<Vec3>(call(tr,"get_position"));
        if(!valid(v))throw std::runtime_error("Invalid transform position");
        return v;
    }

    std::vector<std::string> hierarchyNames(Obj tr){
        std::vector<std::string> names;
        std::set<Obj> seen;
        Obj cur=tr;
        for(int i=0;i<32&&cur&&alive(cur);++i){
            if(seen.contains(cur))break;
            seen.insert(cur);
            Obj go=componentGO(cur);
            names.push_back(objectName(go));
            cur=getParent(cur);
        }
        std::reverse(names.begin(),names.end());
        return names;
    }

    Obj topAncestorGO(Obj compOrGo){
        Obj tr=getTransform(compOrGo);
        if(!tr||!alive(tr))return nullptr;
        if(Method mRoot=tryMethod(unity("Transform"),"get_root",0)){
            try{
                Obj r=invoke(mRoot,tr);
                if(r&&alive(r))return componentGO(r);
            }catch(...){}
        }
        std::set<Obj> seen;
        for(int i=0;i<32;++i){
            if(seen.contains(tr))break;
            seen.insert(tr);
            Obj p=getParent(tr);
            if(!p||!alive(p))break;
            tr=p;
        }
        return componentGO(tr);
    }

    std::int32_t readManagedListVersion(Obj listObj){
        if(!isValidObjectPointer(listObj))return 0;
        std::int32_t ver=0;
        if(readFieldNamed<std::int32_t>(listObj,{"_version","version"},ver))return ver;
        if(a.hasNativeLayout&&safeReadAt(listObj,0x1C,ver))return ver;
        return 0;
    }

    std::vector<Obj> readManagedList(Obj listObj,std::size_t maxItems=2048){
        std::vector<Obj> out;
        if(!isValidObjectPointer(listObj))return out;
        Obj itemsArr=nullptr;
        std::int32_t size=0;
        Klass lk=a.object_get_class(listObj);
        Field fItems=findFieldAny(lk,{"_items","items"});
        Field fSize=findFieldAny(lk,{"_size","size"});
        if(fItems&&fSize&&a.type_get_type(a.field_get_type(fItems))==0x1d&&a.type_get_type(a.field_get_type(fSize))==0x08){
            a.field_get_value(listObj,fItems,&itemsArr);
            a.field_get_value(listObj,fSize,&size);
        }
        if((!isValidObjectPointer(itemsArr)||size<=0)&&a.hasNativeLayout){
            safeReadAt(listObj,0x10,itemsArr);
            safeReadAt(listObj,0x18,size);
        }
        if(!isValidObjectPointer(itemsArr)||size<=0||std::size_t(size)>maxItems)return out;
        std::size_t cap=a.array_length(itemsArr);
        if(cap==0&&a.hasNativeLayout){
            std::uint64_t rawCap=0;
            if(safeReadAt(itemsArr,0x18,rawCap))cap=std::size_t(rawCap);
        }
        if(cap<std::size_t(size))return out;
        auto head=a.array_object_header_size();
        if(head<16||head>128)head=0x20;
        out.reserve(std::size_t(size));
        for(std::int32_t i=0;i<size;++i){
            Obj item=nullptr;
            if(safeReadAt(itemsArr,head+std::size_t(i)*sizeof(Obj),item)&&isValidObjectPointer(item)){
                out.push_back(item);
            }
        }
        return out;
    }

    // 100% non-generic hierarchy traversal using verified dump.cs methods:
    // Transform.get_childCount() (RVA 0xA18D374), Transform.GetChild(int) (RVA 0xA18E660),
    // GameObject.GetComponentCount() (RVA 0xA16E02C), GameObject.QueryComponentAtIndex(int) (RVA 0xA16E118).
    // None of these 4 methods have generic overloads in UnityEngine.CoreModule.dll!
    std::vector<Obj> getComponentsInChildrenByName(Obj rootGo,const char* targetClassName){
        std::vector<Obj> out;
        if(!rootGo||!alive(rootGo)||!targetClassName)return out;
        Obj rootTr=getTransform(rootGo);
        if(!rootTr||!alive(rootTr))return out;

        Method mChildCount=tryMethod(unity("Transform"),"get_childCount",0);
        Method mGetChild=tryMethod(unity("Transform"),"GetChild",1);
        Method mCompCount=tryMethod(unity("GameObject"),"GetComponentCount",0);
        Method mQueryComp=tryMethod(unity("GameObject"),"QueryComponentAtIndex",1);
        if(!mChildCount||!mGetChild||!mCompCount||!mQueryComp)return out;

        std::vector<Obj> stack;
        stack.reserve(256);
        stack.push_back(rootTr);
        std::set<Obj> visited;
        while(!stack.empty()&&visited.size()<4096&&out.size()<2048){
            Obj tr=stack.back();
            stack.pop_back();
            if(!tr||!alive(tr)||visited.contains(tr))continue;
            visited.insert(tr);

            Obj go=componentGO(tr);
            if(go&&alive(go)){
                int cCount=0;
                try{cCount=unbox<int>(invoke(mCompCount,go));}catch(...){cCount=0;}
                for(int i=0;i<cCount&&i<64;++i){
                    void* args[]={&i};
                    try{
                        Obj comp=invoke(mQueryComp,go,args);
                        if(comp&&alive(comp)){
                            Klass ck=a.object_get_class(comp);
                            if(ck&&(classLabel(comp)==targetClassName||inheritsFrom(ck,targetClassName))){
                                out.push_back(comp);
                            }
                        }
                    }catch(...){}
                }
            }

            int chCount=0;
            try{chCount=unbox<int>(invoke(mChildCount,tr));}catch(...){chCount=0;}
            for(int i=chCount-1;i>=0&&i<512;--i){
                void* args[]={&i};
                try{
                    Obj childTr=invoke(mGetChild,tr,args);
                    if(childTr&&alive(childTr))stack.push_back(childTr);
                }catch(...){}
            }
        }
        return out;
    }

    Obj actorSystem(){
        Klass ak=game("ActorSystem");
        if(Field f=tryField(ak,"Self")){
            Obj o=nullptr;
            a.field_static_get_value(f,&o);
            if(isValidObjectPointer(o))return o;
        }
        if(a.hasNativeLayout){
            void*staticFields=nullptr;
            if(safeReadAt(ak,0xB8,staticFields)&&staticFields){
                Obj o=nullptr;
                if(safeReadAt(staticFields,0,o)&&isValidObjectPointer(o))return o;
            }
        }
        return nullptr;
    }
    Obj actor(){
        auto o=actorSystem();
        if(!isValidObjectPointer(o))return nullptr;
        if(Method m=tryMethod(a.object_get_class(o),"get_MyActorCharacter",0)){
            try{
                Obj act=invoke(m,o);
                if(isValidObjectPointer(act))return act;
            }catch(...){}
        }
        if(a.hasNativeLayout){
            Obj act=nullptr;
            if(safeReadAt(o,0x58,act)&&isValidObjectPointer(act))return act;
        }
        return nullptr;
    }

    Obj currentControl(Obj actorObj){
        if(!isValidObjectPointer(actorObj)){controllerName.clear();return nullptr;}
        Obj control=nullptr;
        if(Field fCtrl=tryField(a.object_get_class(actorObj),"actorControl")){
            control=read<Obj>(actorObj,fCtrl);
        }else if(a.hasNativeLayout){
            safeReadAt(actorObj,0x68,control);
        }
        if(!isValidObjectPointer(control)){controllerName.clear();return nullptr;}
        Obj baseAct=nullptr;
        if(Field fBase=tryField(a.object_get_class(control),"actorBase")){
            baseAct=read<Obj>(control,fBase);
        }else if(a.hasNativeLayout){
            safeReadAt(control,0x40,baseAct);
        }
        if(baseAct!=actorObj)throw std::runtime_error("Local controller link changed");
        auto klass=a.object_get_class(control);bool supported=false;
        for(auto c=klass;c;c=a.class_get_parent(c)){
            const char*n=a.class_get_name(c);const char*ns=a.class_get_namespace(c);
            if(n&&(!ns||!*ns)&&std::strcmp(n,"ActorControl")==0){supported=true;break;}
        }
        if(!supported)throw std::runtime_error("Local controller is not an ActorControl");
        bool term=false;
        if(Field fTerm=tryField(klass,"_terminate")){
            term=read<bool>(control,fTerm,2);
        }else if(a.hasNativeLayout){
            std::uint8_t t8=0;
            safeReadAt(control,0xA8,t8);
            term=(t8!=0);
        }
        if(term){controllerName.clear();return nullptr;}
        controllerName=a.class_get_name(klass);return control;
    }

    void resetFocus(){controlRoot.reset();actorRoot.reset();actorUID.clear();nextJump=0;lastJump=-1;}

    // Exact dump.cs & agent.js (kmgLifeState / airRepeatLifeState) offsets for ActorMiniGameControl / ActorKMGPlayer / ActorDefaultControlPlayer:
    // - ActorControl._terminate: +0xA8
    // - ActorMiniGameControl.<IsCanMove>k__BackingField: +0x2A8, <IsCanJump>k__BackingField: +0x2A9
    // - ActorMiniGameControl.<IsDeathInGame>k__BackingField: +0x2B0, <IsGoalIn>k__BackingField: +0x2B2
    // - ActorKMGPlayer: maxHp +0x354 (int32), currentHp +0x358 (int32), watch +0x380 (uint8), state +0x400 (int16), isDie +0x402 (uint8)
    bool checkJumpBlocked(Obj actorObj,Obj control,Klass klass,std::string&reasonOut){
        const bool isMini=isMiniControlName(controllerName)||inheritsFrom(klass,"ActorMiniGameControl");
        if(isMini){
            std::uint8_t goal=0;
            if(!readFieldNamed<std::uint8_t>(control,{"<IsGoalIn>k__BackingField","_isGoalIn","isGoalIn"},goal)){
                if(a.hasNativeLayout&&safeReadAt(control,0x2B2,goal)){}
                else if(Method mGoal=tryMethod(klass,"get_IsGoalIn",0)){
                    try{goal=unbox<bool>(invoke(mGoal,control))?1:0;}catch(...){}
                }
            }

            std::uint8_t death=0;
            if(!readFieldNamed<std::uint8_t>(control,{"<IsDeathInGame>k__BackingField","_isDeathInGame","isDeathInGame"},death)){
                if(a.hasNativeLayout&&safeReadAt(control,0x2B0,death)){}
                else if(Method mDeath=tryMethod(klass,"get_IsDeathInGame",0)){
                    try{death=unbox<bool>(invoke(mDeath,control))?1:0;}catch(...){}
                }
            }
            if(controllerName=="ActorKMGPlayer"){
                std::uint8_t die=0,watch=0;
                std::int16_t kmgState=0;
                if(!readFieldNamed<std::uint8_t>(control,{"isDie","_isDie"},die)&&a.hasNativeLayout){
                    safeReadAt(control,0x402,die);
                }
                if(!readFieldNamed<std::uint8_t>(control,{"<IsUserWatchMode>k__BackingField","isWatchMode","_isWatchMode"},watch)&&a.hasNativeLayout){
                    safeReadAt(control,0x380,watch);
                }
                if(!readFieldNamed<std::int16_t>(control,{"<CurrentCharacterState>k__BackingField","_state","state"},kmgState)&&a.hasNativeLayout){
                    safeReadAt(control,0x400,kmgState);
                }
                if(kmgState>=51&&kmgState<=54){reasonOut="Spam nhảy: đã về đích • dừng nhảy";return true;}
                if(goal==1){
                    // OnGroundHit can set IsGoalIn before server result while player is still active in round (agent.js kmgLifeState)
                    bool goalConfirmed=true;
                    if(a.hasNativeLayout&&die==0&&watch==0&&!(kmgState>=8&&kmgState<=13)&&kmgState!=50){
                        Obj mg=findMiniGame(actorObj);
                        if(mg&&alive(mg)){
                            std::uint8_t success=0;
                            std::int32_t resultStep=0;
                            if(safeReadAt(mg,0x19A,success)&&safeReadAt(mg,0x1C8,resultStep)&&
                               getBattleState(mg)==5&&success==0&&resultStep==0){
                                goalConfirmed=false;
                            }
                        }
                    }
                    if(goalConfirmed){reasonOut="Spam nhảy: đã về đích • dừng nhảy";return true;}
                }
                if(watch==1){reasonOut="Spam nhảy: đang chế độ xem";return true;}
                if(die==1||(kmgState>=8&&kmgState<=13)||kmgState==50){
                    reasonOut="Spam nhảy: chờ nhân vật hồi sinh";return true;
                }
            }else{
                if(goal==1){reasonOut="Spam nhảy: đã về đích • dừng nhảy";return true;}
                if(death==1){reasonOut="Spam nhảy: chờ nhân vật hồi sinh";return true;}
            }
        }
        return false;
    }

    bool updateJoyStickState(Obj actorObj,double time,ImVec2&jumpBtnPosOut,std::string&blockedReasonOut){
        const bool allowSlow=(joyStickScanTime<0||(time-joyStickScanTime)>=0.50);
        if(allowSlow)joyStickScanTime=time;
        Obj dlg=findDialogByClass("DialogJoyStick",joyStickDialogRoot,false);
        if(!dlg||!alive(dlg))return true;

        if(a.hasNativeLayout){
            Obj ownerActor=nullptr;
            if(safeReadAt(dlg,0x1F0,ownerActor)&&ownerActor&&ownerActor!=actorObj){
                return true;
            }
            std::uint8_t inVehicle=0,inSheetMusic=0;
            Obj actionLink=nullptr;
            safeReadAt(dlg,0x1F8,inVehicle);
            safeReadAt(dlg,0x209,inSheetMusic);
            safeReadAt(dlg,0x218,actionLink);
            if(inVehicle==1||inSheetMusic==1||isValidObjectPointer(actionLink)){
                blockedReasonOut="Spam nhảy: tạm dừng khi lái xe / tương tác";
                return false;
            }
        }

        // Resolve Jump button Transform inside DialogJoyStick once per dialog instance
        // dumpvng.cs & dumpqt.cs: `public JoyStickButton jumpButton; // 0x140`
        Obj jtr=jumpBtnTrRoot.get();
        if(!jtr||!alive(jtr)){
            jumpBtnTrRoot.reset();
            Obj jb=readObjFieldNamed(dlg,{"jumpButton","_jumpButton"});
            if((!jb||!alive(jb))&&a.hasNativeLayout)safeReadAt(dlg,0x140,jb);
            if(jb&&alive(jb)){
                if(Obj jbTr=getTransform(jb)){
                    jumpBtnTrRoot.set(a,jbTr);
                    jtr=jbTr;
                }
            }
            if(!jtr&&allowSlow){
                Obj dlgGo=componentGO(dlg);
                Obj dlgTr=getTransform(dlgGo);
                Method mChildCount=tryMethod(unity("Transform"),"get_childCount",0);
                Method mGetChild=tryMethod(unity("Transform"),"GetChild",1);
                if(dlgTr&&mChildCount&&mGetChild){
                    std::vector<Obj> stack={dlgTr};
                    std::set<Obj> seen;
                    while(!stack.empty()&&seen.size()<192){
                        Obj tr=stack.back();stack.pop_back();
                        if(!tr||!alive(tr)||seen.contains(tr))continue;
                        seen.insert(tr);
                        Obj go=componentGO(tr);
                        if(!go||!alive(go)||!activeGO(go))continue;
                        std::string nm=objectName(go);
                        if(nm.find("Jump")!=std::string::npos||nm.find("jump")!=std::string::npos){
                            jumpBtnTrRoot.set(a,tr);
                            jtr=tr;
                            break;
                        }
                        int chCount=0;
                        try{chCount=unbox<int>(invoke(mChildCount,tr));}catch(...){chCount=0;}
                        for(int i=chCount-1;i>=0&&i<64;--i){
                            void*args[]={&i};
                            try{
                                Obj ch=invoke(mGetChild,tr,args);
                                if(ch&&alive(ch))stack.push_back(ch);
                            }catch(...){}
                        }
                    }
                }
            }
        }

        if(jtr&&alive(jtr)&&activeGO(componentGO(jtr))){
            try{
                Vec3 wp=worldPos(jtr);
                if(Klass scrKlass=tryKlass("UnityEngine.CoreModule.dll","UnityEngine","Screen")){
                    Method mW=tryMethod(scrKlass,"get_width",0);
                    Method mH=tryMethod(scrKlass,"get_height",0);
                    if(mW&&mH){
                        int sw=unbox<int>(invoke(mW,nullptr));
                        int sh=unbox<int>(invoke(mH,nullptr));
                        if(sw>200&&sh>200&&wp.x>=float(sw)*0.68f&&wp.x<=float(sw)*0.98f&&wp.y>=float(sh)*0.02f&&wp.y<=float(sh)*0.42f){
                            jumpBtnPosOut={wp.x/float(sw),1.f-(wp.y/float(sh))};
                        }
                    }
                }
            }catch(...){}
        }
        return true;
    }

    void jumpTick(Obj actorObj,double time,double intervalMs,std::string&jumpNoteOut,bool jumpPressed,ImVec2&jumpBtnPosOut){
        if(!std::isfinite(intervalMs)||intervalMs<0||intervalMs>1000)throw std::runtime_error("Invalid spam interval");
        auto control=currentControl(actorObj);
        if(!control){controlRoot.reset();nextJump=0;lastJump=-1;return;}
        if(control!=controlRoot.get()){controlRoot.set(a,control);nextJump=0;lastJump=-1;}
        auto klass=a.object_get_class(control);
        std::string blockedReason;
        if(checkJumpBlocked(actorObj,control,klass,blockedReason)||!updateJoyStickState(actorObj,time,jumpBtnPosOut,blockedReason)){
            jumpNoteOut=blockedReason;
            return;
        }
        if(!jumpPressed){
            nextJump=0;
            jumpNoteOut="Spam nhảy: BẬT • Đè nút Nhảy để spam";
            return;
        }
        if(time<nextJump||time==lastJump){
            jumpNoteOut="Spam nhảy: ĐANG NHẢY • Giữ nút để tiếp tục";
            return;
        }

        // Exact dumpvng.cs & dumpqt.cs field names & offsets:
        // ActorDefaultControlPlayer: `_isCamJump` (+0x4E3; +0x4E2 is _isCanMove, +0x4E1 is _skipMotionCB)
        // ActorMiniGameControl: `<IsCanJump>k__BackingField` (+0x2A9)
        const bool isMini=isMiniControlName(controllerName)||inheritsFrom(klass,"ActorMiniGameControl");
        const std::size_t nativeCanJumpOff=isMini?0x2A9:(controllerName=="ActorDefaultControlPlayer"?0x4E3:0);
        Field canJumpField=findFieldAny(klass,{"_canJump","_isCamJump","<IsCanJump>k__BackingField","canJump","_isCanJump","isCanJump"});
        std::uint8_t origCanJump=1;
        bool liftedField=false;
        bool liftedNative=false;

        if(canJumpField){
            a.field_get_value(control,canJumpField,&origCanJump);
            if(origCanJump==0){
                std::uint8_t one=1;
                liftedField=writeField(control,canJumpField,one);
            }
        }else if(a.hasNativeLayout&&nativeCanJumpOff!=0){
            if(safeReadAt(control,nativeCanJumpOff,origCanJump)&&origCanJump==0){
                std::uint8_t one=1;
                liftedNative=safeWriteAt(control,nativeCanJumpOff,one);
            }
        }

        struct RestoreGuard {
            State* s;Obj ctrl;Field f;std::size_t nativeOff;std::uint8_t orig;bool byField;bool byNative;
            ~RestoreGuard(){
                if(byField)s->writeField(ctrl,f,orig);
                else if(byNative&&nativeOff!=0)safeWriteAt(ctrl,nativeOff,orig);
            }
        } restoreGuard{this,control,canJumpField,nativeCanJumpOff,origCanJump,liftedField,liftedNative};

        call(control,"OnJump");
        ++jumpRequests;lastJump=time;nextJump=time+intervalMs/1000.;
        jumpNoteOut="Spam nhảy: ĐANG NHẢY • Giữ nút để tiếp tục";
    }

    Obj findActiveComponentInChildren(Obj rootGo,const char*targetClassName){
        if(!rootGo||!alive(rootGo)||!activeGO(rootGo)||!targetClassName)return nullptr;
        Obj rootTr=getTransform(rootGo);
        if(!rootTr||!alive(rootTr))return nullptr;
        Method mChildCount=tryMethod(unity("Transform"),"get_childCount",0);
        Method mGetChild=tryMethod(unity("Transform"),"GetChild",1);
        Method mCompCount=tryMethod(unity("GameObject"),"GetComponentCount",0);
        Method mQueryComp=tryMethod(unity("GameObject"),"QueryComponentAtIndex",1);
        if(!mChildCount||!mGetChild||!mCompCount||!mQueryComp)return nullptr;

        struct Node {Obj tr;int depth;};
        std::vector<Node> stack;
        stack.reserve(64);
        stack.push_back({rootTr,0});
        std::set<Obj> visited;
        while(!stack.empty()&&visited.size()<96){
            auto[tr,depth]=stack.back();
            stack.pop_back();
            if(!tr||!alive(tr)||visited.contains(tr))continue;
            visited.insert(tr);
            Obj go=componentGO(tr);
            if(!go||!alive(go)||!activeGO(go))continue;

            // Only query components on GameObjects whose name matches targetClassName (or at root depth <= 1)
            std::string nm=objectName(go);
            if(depth<=1||nm.find(targetClassName)!=std::string::npos){
                int cCount=0;
                try{cCount=unbox<int>(invoke(mCompCount,go));}catch(...){cCount=0;}
                for(int i=0;i<cCount&&i<16;++i){
                    void*args[]={&i};
                    try{
                        Obj comp=invoke(mQueryComp,go,args);
                        if(comp&&alive(comp)&&classLabel(comp)==targetClassName)return comp;
                    }catch(...){}
                }
            }

            if(depth>=4)continue;
            int chCount=0;
            try{chCount=unbox<int>(invoke(mChildCount,tr));}catch(...){chCount=0;}
            for(int i=chCount-1;i>=0&&i<64;--i){
                void*args[]={&i};
                try{
                    Obj childTr=invoke(mGetChild,tr,args);
                    if(childTr&&alive(childTr))stack.push_back({childTr,depth+1});
                }catch(...){}
            }
        }
        return nullptr;
    }

    Obj readSingletonInstance(Klass k,const char*expectedClassName){
        if(!k||!expectedClassName)return nullptr;
        Klass cur=k;
        for(int step=0;step<2&&cur;++step,cur=a.class_get_parent(cur)){
            const char*cn=a.class_get_name(cur);
            if(cn&&(std::strcmp(cn,"Dialog")==0||std::strcmp(cn,"UIBase")==0||
                    std::strcmp(cn,"MonoBehaviour")==0||std::strcmp(cn,"Component")==0||
                    std::strcmp(cn,"Object")==0))break;
            if(a.hasNativeLayout){
                const bool isSingletonHolder=(step==0)||(cn&&(std::strncmp(cn,"SingleTon",9)==0||std::strncmp(cn,"Singleton",9)==0));
                if(isSingletonHolder){
                    void*staticFields=nullptr;
                    if(safeReadAt(cur,0xB8,staticFields)&&staticFields){
                        Obj cand=nullptr;
                        if(safeReadAt(staticFields,0,cand)&&isValidObjectPointer(cand)&&classLabel(cand)==expectedClassName){
                            return cand;
                        }
                    }
                }
            }
            for(const char*sname:{"Self","Instance","_instance","s_Instance"}){
                if(Field sf=a.class_get_field_from_name(cur,sname)){
                    alignas(16) unsigned char scratch[64]{};
                    a.field_static_get_value(sf,scratch);
                    Obj val=nullptr;
                    std::memcpy(&val,scratch,sizeof(Obj));
                    if(isValidObjectPointer(val)&&classLabel(val)==expectedClassName){
                        return val;
                    }
                }
            }
        }
        return nullptr;
    }

    bool isMyHomeLoading(){
        Klass hk=tryGame("MyHomeObjectManager");
        if(!hk)return false;
        Obj mgr=readSingletonInstance(hk,"MyHomeObjectManager");
        if(!mgr||!isValidObjectPointer(mgr))return false;
        std::int32_t loadState=0;
        std::uint8_t isCloseFlow=0;
        if(a.hasNativeLayout){
            // dump.cs MyHomeObjectManager: _loadState at +0x38 (1=SetData, 2=Loading, 3=Complete), _isCloseFlow at +0x3C
            safeReadAt(mgr,0x38,loadState);
            safeReadAt(mgr,0x3C,isCloseFlow);
        }else{
            readFieldNamed<std::int32_t>(mgr,{"_loadState","loadState"},loadState);
            readFieldNamed<std::uint8_t>(mgr,{"_isCloseFlow","isCloseFlow"},isCloseFlow);
        }
        return (loadState==1||loadState==2||isCloseFlow!=0);
    }

    Obj findUISystem(){
        Obj cached=uiSystemRoot.get();
        if(cached&&isValidObjectPointer(cached))return cached;
        uiSystemRoot.reset();
        // 1. Check FrameWork.Self -> sysDialog (+0x20, DialogSystem in dump.cs)
        if(Klass fwk=tryGame("FrameWork")){
            if(Obj fw=readSingletonInstance(fwk,"FrameWork")){
                Obj sysDlg=nullptr;
                if(a.hasNativeLayout){
                    safeReadAt(fw,0x20,sysDlg);
                }else{
                    sysDlg=readObjFieldNamed(fw,{"sysDialog","_sysDialog"});
                }
                if(isValidObjectPointer(sysDlg)&&classLabel(sysDlg)=="DialogSystem"){
                    uiSystemRoot.set(a,sysDlg);
                    return sysDlg;
                }
            }
        }
        // 2. Check DialogSystem / UISystem / UIManager singletons
        for(const char*sysName:{"DialogSystem","UISystem","UIManager"}){
            if(Klass uk=tryGame(sysName)){
                if(Obj val=readSingletonInstance(uk,sysName)){
                    uiSystemRoot.set(a,val);
                    return val;
                }
            }
        }
        return nullptr;
    }

    Obj findDialogByClass(const char*targetClassName,Root&cacheRoot,bool allowSlowScan){
        Obj cached=cacheRoot.get();
        if(cached&&isValidObjectPointer(cached)&&classLabel(cached)==targetClassName&&alive(cached))return cached;
        cacheRoot.reset();

        Klass k=tryGame(targetClassName);
        if(!k)return nullptr;

        // 1. Check static singleton fields on the class itself and its immediate parent (SingleTonDialog<T>)
        // Always verify classLabel(cand)==targetClassName BEFORE calling alive(cand)!
        if(Obj cand=readSingletonInstance(k,targetClassName)){
            if(alive(cand)){
                cacheRoot.set(a,cand);
                return cand;
            }
        }

        // DialogGroupPlay is strictly a SingleTonDialog<DialogGroupPlay> (agent.js line 243)
        if(!allowSlowScan||std::strcmp(targetClassName,"DialogGroupPlay")==0)return nullptr;

        // 2. Safe DialogSystem / UISystem reference & List scan (never call alive() on non-matching classes, never walk Transforms)
        if(Obj uiSys=findUISystem()){
            if(Obj direct=findRefByClass(uiSys,targetClassName,{"_minigameSimpleInfo","minigameSimpleInfo","_currentDialog","currentDialog"})){
                if(isValidObjectPointer(direct)&&classLabel(direct)==targetClassName&&alive(direct)){
                    cacheRoot.set(a,direct);
                    return direct;
                }
            }
            if(a.class_get_fields&&a.field_get_offset){
                Klass uk=a.object_get_class(uiSys);
                for(Klass ucur=uk;ucur;ucur=a.class_get_parent(ucur)){
                    const char*ucn=a.class_get_name(ucur);
                    if(ucn&&(std::strcmp(ucn,"MonoBehaviour")==0||std::strcmp(ucn,"Component")==0||std::strcmp(ucn,"Object")==0))break;
                    void*iter=nullptr;
                    while(Field f=a.class_get_fields(ucur,&iter)){
                        if(!isInstanceField(f))continue;
                        int t=a.type_get_type(a.field_get_type(f));
                        if(t!=0x12&&t!=0x15&&t!=0x1c)continue;
                        Obj val=nullptr;
                        if(!safeReadAt(uiSys,a.field_get_offset(f),val)||!isValidObjectPointer(val))continue;
                        Klass vk=a.object_get_class(val);
                        if(!vk)continue;
                        if(classLabel(val)==targetClassName&&alive(val)){
                            cacheRoot.set(a,val);
                            return val;
                        }
                        if(inheritsFrom(vk,"List`1")){
                            for(Obj item:readManagedList(val,128)){
                                if(item&&isValidObjectPointer(item)&&classLabel(item)==targetClassName&&alive(item)){
                                    cacheRoot.set(a,item);
                                    return item;
                                }
                            }
                        }
                    }
                }
            }
        }
        return nullptr;
    }

    Obj resolveButtonComponent(Obj button){
        if(!button||!isValidObjectPointer(button)||!alive(button))return nullptr;
        Klass bk=a.object_get_class(button);
        if(bk&&inheritsFrom(bk,"Button"))return button;
        if(Obj bgo=componentGO(button)){
            if(Method mCompCount=tryMethod(unity("GameObject"),"GetComponentCount",0)){
                if(Method mQueryComp=tryMethod(unity("GameObject"),"QueryComponentAtIndex",1)){
                    int cCount=0;
                    try{cCount=unbox<int>(invoke(mCompCount,bgo));}catch(...){cCount=0;}
                    for(int i=0;i<cCount&&i<12;++i){
                        void*args[]={&i};
                        try{
                            Obj c=invoke(mQueryComp,bgo,args);
                            if(c&&isValidObjectPointer(c)&&alive(c)&&inheritsFrom(a.object_get_class(c),"Button"))return c;
                        }catch(...){}
                    }
                }
            }
        }
        return nullptr;
    }

    bool invokeReadyAction(Obj dialog,Obj button){
        // 1. Primary path: invoke 0-arg Ready callback on the dialog class itself (DialogMinigameSimpleInfo / DialogGroupPlay)
        if(dialog&&isValidObjectPointer(dialog)&&alive(dialog)){
            Klass dk=a.object_get_class(dialog);
            for(const char*mname:{"OnClickReady","OnClick_Ready","OnClickReadyButton","OnClick_ReadyButton",
                                  "OnClickReadyBtn","OnClick_ReadyBtn","OnClickJoin","OnClick_Join",
                                  "OnClickGameJoin","OnClick_GameJoin","OnReady","Ready"}){
                Method m=tryMethod(dk,mname,0);
                if(m&&isExecutableMethod(m)){
                    try{invoke(m,dialog);++readyRequests;return true;}catch(...){return false;}
                }
            }
            if(a.class_get_methods&&a.method_get_name&&a.method_get_param_count){
                void*iter=nullptr;
                while(Method m=a.class_get_methods(dk,&iter)){
                    if(!isExecutableMethod(m)||a.method_get_param_count(m)!=0)continue;
                    const char*mn=a.method_get_name(m);
                    if(!mn)continue;
                    std::string s(mn);
                    if((s.find("Ready")!=std::string::npos||s.find("ready")!=std::string::npos)&&
                       s.find("Cancel")==std::string::npos&&s.find("Close")==std::string::npos&&
                       s.find("UnReady")==std::string::npos&&s.find("NotReady")==std::string::npos&&
                       s.find("Other")==std::string::npos&&s.find("Receive")==std::string::npos&&
                       s.find("Set")==std::string::npos&&s.find("Update")==std::string::npos&&
                       s.find("Init")==std::string::npos&&s.find("Refresh")==std::string::npos&&
                       s.find("get_")!=0&&s.find("set_")!=0&&s.find("<")==std::string::npos){
                        try{invoke(m,dialog);++readyRequests;return true;}catch(...){return false;}
                    }
                }
            }
        }

        // 2. Secondary path: invoke UnityEngine.UI.Button.m_OnClick.Invoke() on the Ready button (+0x138 / +0x188)
        if(Obj btnComp=resolveButtonComponent(button)){
            Obj onClickEvt=readObjFieldNamed(btnComp,{"m_OnClick","onClick"});
            if(!onClickEvt){
                if(Method mGetClick=tryMethod(a.object_get_class(btnComp),"get_onClick",0)){
                    try{onClickEvt=invoke(mGetClick,btnComp);}catch(...){onClickEvt=nullptr;}
                }
            }
            if(onClickEvt&&isValidObjectPointer(onClickEvt)){
                if(Method mInv=tryMethod(a.object_get_class(onClickEvt),"Invoke",0)){
                    try{invoke(mInv,onClickEvt);++readyRequests;return true;}catch(...){return false;}
                }
            }
        }

        // 3. House Party fallback (dump.cs MyHomeGameManager: _isReadyGame at +0x3D, _isLoadDialogMinigameInfo at +0x48, OnClick_GameJoin)
        if(dialog&&classLabel(dialog)=="DialogMinigameSimpleInfo"){
            if(Klass hgk=tryGame("MyHomeGameManager")){
                if(Obj hgm=readSingletonInstance(hgk,"MyHomeGameManager")){
                    std::uint8_t isReadyGame=0,isLoadedInfo=0;
                    if(a.hasNativeLayout){
                        safeReadAt(hgm,0x3D,isReadyGame);
                        safeReadAt(hgm,0x48,isLoadedInfo);
                    }
                    if(isReadyGame==0&&isLoadedInfo!=0){
                        Method mJoin=a.class_get_method_from_name(hgk,"OnClick_GameJoin",0);
                        if(mJoin&&isExecutableMethod(mJoin)){
                            try{invoke(mJoin,hgm);++readyRequests;return true;}catch(...){return false;}
                        }
                    }
                }
            }
        }
        return false;
    }

    bool simpleReadyTick(double time,bool allowSlowScan){
        // Offsets from agent.js inspectSimpleMinigameDialog (lines 211-238):
        // button: +0x138, isLocked: +0x162, isReady: +0x163, usesTickets: +0x16C, handler: +0x170
        Obj dialog=findDialogByClass("DialogMinigameSimpleInfo",simpleDialogRoot,allowSlowScan);
        if(!dialog||!alive(dialog)){simpleLatched=false;simpleEligibleSince=-1;return false;}
        if(!activeGO(componentGO(dialog))){simpleLatched=false;simpleEligibleSince=-1;return false;}

        Obj button=nullptr;
        if(a.hasNativeLayout){
            safeReadAt(dialog,0x138,button);
        }
        if(!isValidObjectPointer(button)){
            button=readObjFieldNamed(dialog,{"buttonGameReady","_readyButton","readyButton","btnReady","_btnReady","m_btnReady"});
        }
        if(!button||!isValidObjectPointer(button)||!alive(button)||!activeGO(componentGO(button))){
            simpleLatched=false;
            simpleEligibleSince=-1;
            return false;
        }

        std::uint8_t isReady=0,isLocked=0,usesTickets=0;
        if(a.hasNativeLayout){
            safeReadAt(dialog,0x163,isReady);
            safeReadAt(dialog,0x162,isLocked);
            safeReadAt(dialog,0x16C,usesTickets);
        }else{
            readFieldNamed<std::uint8_t>(dialog,{"_isReadyState","_isReady","isReady","<IsReady>k__BackingField"},isReady);
            readFieldNamed<std::uint8_t>(dialog,{"_isLock","isLock","_isWait","isWait"},isLocked);
            readFieldNamed<std::uint8_t>(dialog,{"_useTicektGame","_useTicketGame","_isUseTicket","isUseTicket","usesTickets"},usesTickets);
        }

        if(isReady!=0){
            readySummary="Tự sẵn sàng: Game báo đã sẵn sàng";
            return true;
        }
        if(isLocked!=0){
            simpleEligibleSince=-1;
            readySummary="Tự sẵn sàng: Chờ game mở khóa nút sẵn sàng";
            return true;
        }

        Obj handler=nullptr;
        if(a.hasNativeLayout){
            safeReadAt(dialog,0x170,handler);
            if(!isValidObjectPointer(handler)){
                simpleEligibleSince=-1;
                readySummary="Tự sẵn sàng: Chờ game gắn xử lý nút sẵn sàng";
                return true;
            }
        }

        // Zero-delay instant trigger: execute immediately on Frame 0 when button is unlocked
        if(!simpleLatched || (time >= nextReadyClickTime)){
            simpleLatched=true;
            nextReadyClickTime=time+0.4;
            if(invokeReadyAction(dialog,button)){
                readySummary=usesTickets?"Tự sẵn sàng: Đã bấm Sẵn sàng dùng vé":"Tự sẵn sàng: Đã bấm Sẵn sàng (Cực nhanh)";
            }else{
                readySummary="Tự sẵn sàng: Đã gửi yêu cầu Sẵn sàng";
            }
        }
        return true;
    }

    bool partyReadyTick(double time,bool allowSlowScan){
        // Offsets from agent.js inspectPartyDialog (lines 240-260):
        // SingleTonDialog<DialogGroupPlay> (+0xB8 -> +0x0), button: +0x188, partyState: +0x1D4 (==1), usesTickets: +0x1D8
        Obj dialog=findDialogByClass("DialogGroupPlay",partyDialogRoot,allowSlowScan);
        if(!dialog||!alive(dialog)){partyLatched=false;partyEligibleSince=-1;return false;}
        if(!activeGO(componentGO(dialog))){partyLatched=false;partyEligibleSince=-1;return false;}

        Obj button=nullptr;
        std::int32_t partyState=0;
        std::uint8_t usesTickets=0;
        if(a.hasNativeLayout){
            safeReadAt(dialog,0x188,button);
            safeReadAt(dialog,0x1D4,partyState);
            safeReadAt(dialog,0x1D8,usesTickets);
        }else{
            button=readObjFieldNamed(dialog,{"button_GameReady_Member","_readyButton","readyButton","btnReady","_btnReady","m_btnReady"});
            readFieldNamed<std::int32_t>(dialog,{"groupType","_readyState","readyState"},partyState);
            readFieldNamed<std::uint8_t>(dialog,{"_useTicketGame","_isUseTicket","isUseTicket","usesTickets"},usesTickets);
        }

        const bool eligible=(partyState==1)&&isValidObjectPointer(button)&&alive(button)&&activeGO(componentGO(button));
        if(!eligible){
            partyLatched=false;
            partyEligibleSince=-1;
            if(partyState>1){
                readySummary="Tự sẵn sàng: Đã sẵn sàng Tiệc";
                return true;
            }
            return false;
        }
        // Zero-delay instant trigger: execute immediately on Frame 0 when party button is unlocked
        if(!partyLatched || (time >= nextReadyClickTime)){
            partyLatched=true;
            nextReadyClickTime=time+0.4;
            if(invokeReadyAction(dialog,button)){
                readySummary=usesTickets?"Tự sẵn sàng: Đã bấm Sẵn sàng Tiệc dùng vé":"Tự sẵn sàng: Đã bấm Sẵn sàng Tiệc (Cực nhanh)";
            }else{
                readySummary="Tự sẵn sàng: Đã gửi yêu cầu Sẵn sàng Tiệc";
            }
        }
        return true;
    }

    void readyTick(Obj control,double time,std::string&readyNoteOut){
        if(!control){
            simpleLatched=false;partyLatched=false;
            simpleEligibleSince=-1;partyEligibleSince=-1;
            readySummary="Tự sẵn sàng: Chờ ổn định nhân vật";
            readyNoteOut=readySummary;
            return;
        }
        if(isMyHomeLoading()){
            simpleDialogRoot.reset();partyDialogRoot.reset();uiSystemRoot.reset();
            simpleLatched=false;partyLatched=false;
            simpleEligibleSince=-1;partyEligibleSince=-1;
            readySummary="Tự sẵn sàng: Đang tải nhà • Tạm chờ...";
            readyNoteOut=readySummary;
            return;
        }
        Klass ck=a.object_get_class(control);
        if(isMiniControlName(controllerName)||(ck&&inheritsFrom(ck,"ActorMiniGameControl"))){
            simpleLatched=false;partyLatched=false;
            simpleEligibleSince=-1;partyEligibleSince=-1;
            readySummary="Tự sẵn sàng: Đang trong trận • Tự bật lại khi về sảnh/tiệc";
            readyNoteOut=readySummary;
            return;
        }
        // Ultra-fast scan: scan UI hierarchy every 50ms (10x faster than 500ms), singletons checked every frame
        const bool allowSlowScan=(readyScanTime<0||(time-readyScanTime)>=0.05);
        if(allowSlowScan)readyScanTime=time;
        if(simpleReadyTick(time,allowSlowScan)){
            readyNoteOut=readySummary;
            return;
        }
        if(partyReadyTick(time,allowSlowScan)){
            readyNoteOut=readySummary;
            return;
        }
        readySummary="Tự sẵn sàng: BẬT • Chờ bảng Sẵn sàng (Cực nhanh)";
        readyNoteOut=readySummary;
    }

    void readDuelState(Obj gameObj,std::uint8_t&isAttackOut,std::uint8_t&canAttackOut,std::uint32_t&roundOut,std::uint8_t&endedOut,std::int32_t&scoreOut){
        isAttackOut=0;canAttackOut=0;roundOut=0;endedOut=0;scoreOut=0;
        if(!gameObj||!isValidObjectPointer(gameObj))return;
        Klass gk=a.object_get_class(gameObj);
        if(!readFieldNamed<std::uint8_t>(gameObj,{"<IsStateAttack>k__BackingField","_isStateAttack","isStateAttack","IsStateAttack"},isAttackOut)){
            if(a.hasNativeLayout&&safeReadAt(gameObj,0x1D8,isAttackOut)){}
            else if(Method m=tryMethod(gk,"get_IsStateAttack",0)){
                try{isAttackOut=unbox<bool>(invoke(m,gameObj))?1:0;}catch(...){}
            }
        }
        if(!readFieldNamed<std::uint8_t>(gameObj,{"<IsCanAttack>k__BackingField","_isCanAttack","isCanAttack","IsCanAttack"},canAttackOut)){
            if(a.hasNativeLayout&&safeReadAt(gameObj,0x1D9,canAttackOut)){}
            else if(Method m=tryMethod(gk,"get_IsCanAttack",0)){
                try{canAttackOut=unbox<bool>(invoke(m,gameObj))?1:0;}catch(...){}
            }
        }
        if(!readFieldNamed<std::uint32_t>(gameObj,{"<CurRound>k__BackingField","<CurrentRound>k__BackingField","_curRound","curRound","_currentRound","currentRound","_round","round"},roundOut)){
            if(a.hasNativeLayout&&safeReadAt(gameObj,0x1DC,roundOut)){}
            else if(Method m=tryMethod(gk,"get_CurRound",0)){
                try{roundOut=unbox<std::uint32_t>(invoke(m,gameObj));}catch(...){}
            }else if(Method m2=tryMethod(gk,"get_CurrentRound",0)){
                try{roundOut=unbox<std::uint32_t>(invoke(m2,gameObj));}catch(...){}
            }
        }
        if(!readFieldNamed<std::uint8_t>(gameObj,{"isEndGame","<IsEndGame>k__BackingField","_isEndGame","_isEnd","isEnd"},endedOut)){
            if(a.hasNativeLayout&&safeReadAt(gameObj,0x214,endedOut)){}
            else if(Method m=tryMethod(gk,"get_IsEndGame",0)){
                try{endedOut=unbox<bool>(invoke(m,gameObj))?1:0;}catch(...){}
            }
        }
        if(!readFieldNamed<std::int32_t>(gameObj,{"myScore","<Score>k__BackingField","_score","score"},scoreOut)&&a.hasNativeLayout){
            safeReadAt(gameObj,0x218,scoreOut);
        }
    }

    void hammerTick(Obj actorObj,double time,double delayMs,std::string&hammerNoteOut){
        if(!std::isfinite(delayMs)||delayMs<0.||delayMs>10000.)throw std::runtime_error("Invalid hammer delay");
        Obj gameObj=findMiniGame(actorObj);
        if(!gameObj||!alive(gameObj)){
            hammerWindowKey.clear();hammerFightTime=-1;hammerLatched=false;
            hammerSummary="Tự động búa: BẬT • Chờ vào map Búa (120019)";
            hammerNoteOut=hammerSummary;
            return;
        }
        std::string gLabel=classLabel(gameObj);
        int gId=getMiniGameId(gameObj);
        if(gLabel!="MiniGameKMGDuel"&&gId!=120019){
            hammerWindowKey.clear();hammerFightTime=-1;hammerLatched=false;
            hammerSummary="Tự động búa: BẬT • Chờ map Búa (120019)";
            hammerNoteOut=hammerSummary;
            return;
        }

        std::uint8_t isAttack=0,canAttack=0,ended=0;
        std::uint32_t round=0;
        std::int32_t score=0;
        readDuelState(gameObj,isAttack,canAttack,round,ended,score);
        if(round>100)round=0;

        std::string wKey=std::to_string(reinterpret_cast<std::uintptr_t>(gameObj))+":"+std::to_string(round);
        if(wKey!=hammerWindowKey){
            hammerWindowKey=wKey;
            hammerFightTime=-1;
            hammerLatched=false;
        }
        if(ended==1){
            hammerFightTime=-1;
            hammerLatched=false;
            hammerSummary="Tự động búa: Ván đấu đã kết thúc";
            hammerNoteOut=hammerSummary;
            return;
        }
        if(isAttack!=1||canAttack!=1){
            hammerFightTime=-1;
            hammerLatched=false;
            hammerSummary="Tự động búa: Chờ chữ FIGHT (round "+std::to_string(round)+")";
            hammerNoteOut=hammerSummary;
            return;
        }
        if(hammerLatched){
            hammerSummary="Tự động búa: Đã đập búa #"+std::to_string(hammerRequests)+" (round "+std::to_string(round)+") • Chờ lượt tiếp";
            hammerNoteOut=hammerSummary;
            return;
        }

        if(hammerFightTime<0.)hammerFightTime=time;
        const double dueTime=hammerFightTime+(delayMs/1000.);
        if(time+1e-6<dueTime){
            int remMs=int(std::ceil((dueTime-time)*1000.));
            hammerSummary="Tự động búa: Chờ delay "+std::to_string(remMs)+"ms (round "+std::to_string(round)+")";
            hammerNoteOut=hammerSummary;
            return;
        }

        Obj control=currentControl(actorObj);
        if(!control&&a.hasNativeLayout){
            Obj cand=nullptr;
            if(safeReadAt(gameObj,0x238,cand)&&isValidObjectPointer(cand))control=cand;
        }
        if(!isValidObjectPointer(control)){
            hammerSummary="Tự động búa: Chờ ActorDuelPlayer";
            hammerNoteOut=hammerSummary;
            return;
        }

        std::uint8_t isStun=0;
        Klass ck=a.object_get_class(control);
        if(!readFieldNamed<std::uint8_t>(control,{"<IsStun>k__BackingField","_isStun","isStun","<IsFaint>k__BackingField","_isFaint","isFaint"},isStun)){
            if(a.hasNativeLayout&&safeReadAt(control,0x3C0,isStun)){}
            else if(Method mStun=tryMethod(ck,"get_IsStun",0)){
                try{isStun=unbox<bool>(invoke(mStun,control))?1:0;}catch(...){}
            }
        }
        if(isStun==1){
            hammerSummary="Tự động búa: Nhân vật đang choáng";
            hammerNoteOut=hammerSummary;
            return;
        }

        Method mAct=tryMethod(ck,"OnAction",0);
        if(!mAct)mAct=tryMethod(ck,"OnAttack",0);
        if(!mAct)throw std::runtime_error("Method unavailable: OnAction");

        // Latch before invoking native OnAction so one FIGHT window sends exactly one strike
        hammerLatched=true;
        invoke(mAct,control);
        ++hammerRequests;
        int elapsedMs=int(std::max(0.,std::round((time-hammerFightTime)*1000.)));
        hammerSummary="Tự động búa: ĐÃ ĐẬP BÚA #"+std::to_string(hammerRequests)+" (sau "+std::to_string(elapsedMs)+"ms • đặt "+std::to_string(int(delayMs))+"ms • round "+std::to_string(round)+")";
        hammerNoteOut=hammerSummary;
    }

    Vec3 position(Obj actorObj){
        Obj tr=nullptr;
        if(Field f=tryField(a.object_get_class(actorObj),"baseTransfrom")){
            tr=read<Obj>(actorObj,f);
        }
        if(!tr||!alive(tr))tr=getTransform(actorObj);
        if(!alive(tr))throw std::runtime_error("Transform unavailable");
        return worldPos(tr);
    }

    // Exact dump.cs & agent.js (miniGameOrNull lines 40-54) lookup:
    // 1. `PlayTogether.MiniGame.MiniGameKMGUnit.Self` (static field at +0x0)
    // 2. Fallback via `SystemHelper.get_Map()` -> `m_CurrentMap` -> `currentMiniGameUnit` / `miniGameUnit`
    Obj findMiniGame(Obj actorObj){
        Obj ctrl=currentControl(actorObj);
        if(!ctrl)return nullptr;
        Klass ctrlKlass=a.object_get_class(ctrl);
        const bool inMiniGame=isMiniControlName(controllerName)||inheritsFrom(ctrlKlass,"ActorMiniGameControl");
        if(!inMiniGame){
            miniGameRoot.reset();
            return nullptr;
        }

        Obj cached=miniGameRoot.get();
        if(cached&&alive(cached))return cached;
        miniGameRoot.reset();

        if(Klass kmgUnitKlass=tryGame("MiniGameKMGUnit")){
            if(Field selfField=tryField(kmgUnitKlass,"Self")){
                Obj selfObj=nullptr;
                a.field_static_get_value(selfField,&selfObj);
                if(selfObj&&alive(selfObj)){
                    miniGameRoot.set(a,selfObj);
                    return selfObj;
                }
            }
            if(a.hasNativeLayout){
                void*staticFields=nullptr;
                if(safeReadAt(kmgUnitKlass,0xB8,staticFields)&&staticFields){
                    Obj selfObj=nullptr;
                    if(safeReadAt(staticFields,0,selfObj)&&selfObj&&alive(selfObj)){
                        miniGameRoot.set(a,selfObj);
                        return selfObj;
                    }
                }
            }
        }

        if(Klass sysHelper=tryGame("SystemHelper")){
            if(Method mGetMap=tryMethod(sysHelper,"get_Map",0)){
                try{
                    Obj mapSys=invoke(mGetMap,nullptr);
                    if(mapSys&&alive(mapSys)){
                        Obj curMap=readObjFieldNamed(mapSys,{"m_CurrentMap","_currentMap","currentMap"});
                        if(curMap&&alive(curMap)){
                            Obj mg=readObjFieldNamed(curMap,{"miniGameUnit","currentMiniGameUnit","_miniGameUnit"});
                            if(mg&&alive(mg)){
                                miniGameRoot.set(a,mg);
                                return mg;
                            }
                        }
                    }
                }catch(...){}
            }
        }
        return nullptr;
    }

    // Exact dump.cs & agent.js field & getter on MiniGameKMGUnit:
    // `private uint <KMGTableID>k__BackingField; // +0x110`
    int getMiniGameId(Obj gameObj){
        if(!isValidObjectPointer(gameObj))return 0;
        std::uint32_t id=0;
        if(readFieldNamed<std::uint32_t>(gameObj,{"<KMGTableID>k__BackingField","_miniGameId","miniGameId"},id)&&id>=100000&&id<=999999){
            return int(id);
        }
        if(a.hasNativeLayout&&safeReadAt(gameObj,0x110,id)&&id>=100000&&id<=999999){
            return int(id);
        }
        if(Method m=tryMethod(a.object_get_class(gameObj),"get_KMGTableID",0)){
            try{
                id=unbox<std::uint32_t>(invoke(m,gameObj));
                if(id>=100000&&id<=999999)return int(id);
            }catch(...){}
        }
        return int(id);
    }

    // Exact dump.cs & agent.js field & getter on MiniGameUnit:
    // `protected GameBattleState currentBattleState; // +0x6C` (sbyte / uint8)
    int getBattleState(Obj gameObj){
        std::int8_t st=5;
        if(readFieldNamed<std::int8_t>(gameObj,{"currentBattleState","_battleState","battleState"},st)){
            if(st>=0&&st<=15)return int(st);
        }
        if(a.hasNativeLayout&&safeReadAt(gameObj,0x6C,st)){
            if(st>=0&&st<=15)return int(st);
        }
        if(Method m=tryMethod(a.object_get_class(gameObj),"get_CurrentBattleState",0)){
            try{
                st=unbox<std::int8_t>(invoke(m,gameObj));
                if(st>=0&&st<=15)return int(st);
            }catch(...){}
        }
        return 5;
    }

    // Exact dump.cs field on MiniGameKMGUnit:
    // `public HideObjectManager hideObjectManager; // +0x140`
    Obj getHideObjectManager(Obj gameObj){
        if(a.hasNativeLayout){
            Obj m=nullptr;
            if(safeReadAt(gameObj,0x140,m)&&m&&alive(m))return m;
        }
        if(Obj m=readObjFieldNamed(gameObj,{"hideObjectManager","_hideObjectManager","m_HideObjectManager"})){
            if(alive(m))return m;
        }
        return nullptr;
    }

    // Exact dump.cs fields on HideStampObjectData:
    // `public MeshRenderer ObjectRenderer; // +0x30`
    // `private Collider <ObjectCollider>k__BackingField; // +0x40` (`get_ObjectCollider()`)
    Obj getStampCollider(Obj stampObj){
        if(a.hasNativeLayout){
            Obj col=nullptr;
            if(safeReadAt(stampObj,0x40,col)&&col&&alive(col))return col;
        }
        if(Obj col=readObjFieldNamed(stampObj,{"<ObjectCollider>k__BackingField","_collider","collider"})){
            if(alive(col))return col;
        }
        if(Method m=tryMethod(a.object_get_class(stampObj),"get_ObjectCollider",0)){
            try{
                Obj col=invoke(m,stampObj);
                if(col&&alive(col))return col;
            }catch(...){}
        }
        return nullptr;
    }

    int getStampState(Obj stampObj){
        std::int32_t st=-1;
        if(a.hasNativeLayout&&safeReadAt(stampObj,0x58,st))return st;
        if(readFieldNamed<std::int32_t>(stampObj,{"_curHideState","_state","state"},st))return st;
        if(Method m=tryMethod(a.object_get_class(stampObj),"get_State",0)){
            try{return unbox<std::int32_t>(invoke(m,stampObj));}catch(...){}
        }
        return -1;
    }

    // Exact agent.js tileSurfaceAnchor (lines 468-479):
    // 1. ObjectRenderer (+0x30) -> get_bounds
    // 2. ObjectCollider (+0x40) -> get_bounds
    bool tileSurfaceAnchor(Obj stampObj,Vec3&outAnchor){
        Obj ren=nullptr;
        if(a.hasNativeLayout)safeReadAt(stampObj,0x30,ren);
        if(!ren||!alive(ren))ren=readObjFieldNamed(stampObj,{"ObjectRenderer","_renderer","renderer","meshRenderer"});
        if(ren&&alive(ren)){
            try{
                Bounds b=unbox<Bounds>(call(ren,"get_bounds"));
                if(validBounds(b)&&b.extents.x>1e-5f&&b.extents.z>1e-5f){
                    outAnchor={b.center.x,b.center.y+b.extents.y,b.center.z};
                    return true;
                }
            }catch(...){}
        }
        Obj col=getStampCollider(stampObj);
        if(col&&alive(col)){
            try{
                Bounds b=boundsOfCollider(col);
                if(b.extents.x>1e-5f&&b.extents.z>1e-5f){
                    outAnchor={b.center.x,b.center.y+b.extents.y,b.center.z};
                    return true;
                }
            }catch(...){}
        }
        if(!a.hasNativeLayout){
            Obj tr=getTransform(stampObj);
            if(tr&&alive(tr)){
                try{
                    outAnchor=worldPos(tr);
                    return valid(outAnchor);
                }catch(...){}
            }
        }
        return false;
    }

    bool inactiveHoleBounds(Obj col,Bounds&outBounds){
        if(!col||!alive(col))return false;
        std::string type=classLabel(col);
        Bounds localB{};
        try{
            if(type=="BoxCollider"){
                localB.center=unbox<Vec3>(call(col,"get_center"));
                Vec3 sz=unbox<Vec3>(call(col,"get_size"));
                localB.extents={sz.x*0.5f,sz.y*0.5f,sz.z*0.5f};
            }else if(type=="MeshCollider"){
                Obj mesh=call(col,"get_sharedMesh");
                if(!mesh||!alive(mesh))return false;
                localB=unbox<Bounds>(call(mesh,"get_bounds"));
            }else return false;
        }catch(...){return false;}

        Obj tr=getTransform(componentGO(col));
        if(!tr||!alive(tr))return false;

        Vec3 lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
        for(int dx:{-1,1})for(int dy:{-1,1})for(int dz:{-1,1}){
            Vec3 corner{
                localB.center.x+float(dx)*localB.extents.x,
                localB.center.y+float(dy)*localB.extents.y,
                localB.center.z+float(dz)*localB.extents.z
            };
            void*args[]={&corner};
            Vec3 wp{};
            try{wp=unbox<Vec3>(call(tr,"TransformPoint",1,args));}catch(...){return false;}
            if(!valid(wp))return false;
            lo.x=std::min(lo.x,wp.x);lo.y=std::min(lo.y,wp.y);lo.z=std::min(lo.z,wp.z);
            hi.x=std::max(hi.x,wp.x);hi.y=std::max(hi.y,wp.y);hi.z=std::max(hi.z,wp.z);
        }
        outBounds.center={(lo.x+hi.x)*0.5f,(lo.y+hi.y)*0.5f,(lo.z+hi.z)*0.5f};
        outBounds.extents={(hi.x-lo.x)*0.5f,(hi.y-lo.y)*0.5f,(hi.z-lo.z)*0.5f};
        return validBounds(outBounds)&&outBounds.extents.x>0.f&&outBounds.extents.z>0.f;
    }

    // 1. Map 120018 (Kính Thật/Giả) & MiniGameKMGRealBoard (Ván Thật/Giả)
    // Exact agent.js logic (lines 446-482, 3152-3245):
    // - boardRoot = isGlass ? manager : manager->BoardRootTransform (+0xA8)
    // - Scan all HideStampObjectData in children (including inactive GameObjects for state==5 StableStateObject at +0x48!)
    void collectRealBoardOrGlass(Obj gameObj,bool isGlass){
        Obj manager=getHideObjectManager(gameObj);
        if(!manager){espSummary="Kính/Ván: chờ HideObjectManager";return;}

        std::string rKey=std::to_string(reinterpret_cast<std::uintptr_t>(gameObj))+":"+
            std::to_string(reinterpret_cast<std::uintptr_t>(manager))+":"+(isGlass?"G":"B");
        if(boardCache.key!=rKey||!boardCache.ready){
            boardCache=BoardState{};
            boardCache.key=rKey;

            Obj boardRoot=manager;
            if(!isGlass){
                Obj br=nullptr;
                if(a.hasNativeLayout)safeReadAt(manager,0xA8,br);
                if(!br||!alive(br))br=readObjFieldNamed(manager,{"BoardRootTransform","_boardRoot","boardRoot"});
                if(br&&alive(br))boardRoot=br;
            }
            std::vector<Obj> stamps;
            if(Obj rootGo=componentGO(boardRoot)){
                stamps=getComponentsInChildrenByName(rootGo,"HideStampObjectData");
            }
            // Also merge any HideStampObjectData from manager + 0x40 (hideStampGameObjectList)
            Obj listObj=nullptr;
            if(a.hasNativeLayout)safeReadAt(manager,0x40,listObj);
            if(!isValidObjectPointer(listObj)){
                listObj=readObjFieldNamed(manager,{"hideStampGameObjectList","hideStampObjectDataList","_hideStampObjectDataList"});
            }
            for(Obj item:readManagedList(listObj,2048)){
                if(!item||!alive(item))continue;
                Obj s=item;
                if(classLabel(item)=="GameObject"){
                    auto cs=getComponentsInChildrenByName(item,"HideStampObjectData");
                    if(!cs.empty())s=cs.front();
                }
                if(s&&alive(s)&&classLabel(s)=="HideStampObjectData"){
                    if(std::find(stamps.begin(),stamps.end(),s)==stamps.end())stamps.push_back(s);
                }
            }

            if(stamps.empty()){espSummary="Kính/Ván: chờ dữ liệu ô";return;}
            boardCache.tiles.reserve(stamps.size());
            for(Obj obj:stamps){
                if(!obj||!alive(obj))continue;
                BoardTileRec rec{};
                rec.comp.set(a,obj);
                if(Obj go=componentGO(obj))rec.go.set(a,go);
                if(Obj col=getStampCollider(obj))rec.col.set(a,col);
                Vec3 anc{};
                if(tileSurfaceAnchor(obj,anc)){rec.anchor=anc;rec.hasAnchor=true;}
                if(isGlass){
                    Obj stGo=nullptr;
                    if(a.hasNativeLayout)safeReadAt(obj,0x48,stGo);
                    if(!stGo||!alive(stGo))stGo=readObjFieldNamed(obj,{"StableStateObject","stableObject","_stableObject"});
                    if(stGo&&alive(stGo))rec.stableGo.set(a,stGo);
                }
                boardCache.tiles.push_back(std::move(rec));
            }
            boardCache.ready=!boardCache.tiles.empty();
        }

        std::set<Obj> seenColliders;
        int greenCount=0,redCount=0;
        for(auto&rec:boardCache.tiles){
            Obj obj=rec.comp.get();
            if(!obj||!alive(obj))continue;
            int state=getStampState(obj);
            if(state<0||state>5)continue;

            if(isGlass&&state==5){
                Obj stableGo=rec.stableGo.get();
                if(!stableGo&&a.hasNativeLayout){
                    if(safeReadAt(obj,0x48,stableGo)&&stableGo&&alive(stableGo))rec.stableGo.set(a,stableGo);
                }
                if(stableGo&&alive(stableGo)&&activeGO(stableGo)){
                    if(!rec.hasStable){
                        auto cols=getComponentsInChildrenByName(stableGo,"Collider");
                        Obj bestCol=nullptr;float bestArea=0.f;Vec3 bestAnchor{};
                        for(Obj c:cols){
                            if(!solidCollider(c))continue;
                            try{
                                Bounds b=boundsOfCollider(c);
                                if(b.extents.x<=1e-5f||b.extents.z<=1e-5f||b.extents.y>std::min(b.extents.x,b.extents.z))continue;
                                float area=b.extents.x*b.extents.z;
                                if(area>bestArea){bestArea=area;bestCol=c;bestAnchor={b.center.x,b.center.y+b.extents.y,b.center.z};}
                            }catch(...){}
                        }
                        if(bestCol){
                            rec.stableCol.set(a,bestCol);
                            rec.stableAnchor=bestAnchor;
                            rec.hasStable=true;
                        }
                    }
                    Obj bestCol=rec.stableCol.get();
                    if(rec.hasStable&&bestCol&&alive(bestCol)&&!seenColliders.contains(bestCol)){
                        seenColliders.insert(bestCol);
                        worldMarkers.push_back({rec.stableAnchor,"green","",std::to_string(reinterpret_cast<std::uintptr_t>(bestCol)),-1,false,true,false});
                        ++greenCount;
                        continue;
                    }
                }
            }

            Obj go=rec.go.get();
            if(!go||!activeGO(go))continue;
            Obj col=rec.col.get();
            if(!col||!alive(col))col=getStampCollider(obj);
            if(!col||!alive(col)||!activeGO(componentGO(col)))continue;
            if(seenColliders.contains(col))continue;

            bool solid=(state==5)?solidCollider(col):false;
            std::string color=(state==5&&solid)?"green":"red";
            if(!rec.hasAnchor){
                Vec3 anc{};
                if(tileSurfaceAnchor(obj,anc)){rec.anchor=anc;rec.hasAnchor=true;}
                else continue;
            }
            seenColliders.insert(col);
            worldMarkers.push_back({rec.anchor,color,"",std::to_string(reinterpret_cast<std::uintptr_t>(col)),-1,false,true,false});
            if(color=="green")++greenCount;else ++redCount;
        }
        std::sort(worldMarkers.begin(),worldMarkers.end(),[](const WorldMarker&a,const WorldMarker&b){return a.key<b.key;});
        espSummary=std::string(isGlass?"Kính 120018":"Ván Thật/Giả")+": "+std::to_string(greenCount)+" xanh, "+std::to_string(redCount)+" đỏ";
    }

    // 2. Map 120026 (Obby Race *3 Vàng / VCNV02) - Zero-lag cached implementation
    void collectRoad120026(Obj gameObj,int battleState){
        Obj manager=getHideObjectManager(gameObj);
        if(!manager){espSummary="120026: chờ HideObjectManager";return;}

        Obj hideList=nullptr;
        if(a.hasNativeLayout)safeReadAt(manager,0x38,hideList);
        if(!isValidObjectPointer(hideList))hideList=readObjFieldNamed(manager,{"hideGameObjectList","_hideGameObjectList"});
        std::int32_t listVer=readManagedListVersion(hideList);
        std::string rKey=std::to_string(reinterpret_cast<std::uintptr_t>(gameObj))+":"+
            std::to_string(reinterpret_cast<std::uintptr_t>(manager))+":"+std::to_string(listVer);

        // Step 1: Cache the 27 Road_XX root GameObjects once per round without full hierarchy walks
        if(road120026.key!=rKey||road120026.vars.size()!=27){
            road120026=Road120026State{};
            road120026.key=rKey;
            auto objects=readManagedList(hideList,2048);
            if(objects.empty()){espSummary="120026: chờ danh sách đường";return;}
            for(Obj go:objects){
                if(!go||!alive(go))continue;
                std::string nm=objectName(go);
                if(nm.size()==7&&nm.compare(0,5,"Road_")==0){
                    int num=parseDigits(nm,5,2);
                    if(num>=1&&num<=27){
                        Obj pTr=getParent(getTransform(go));
                        if(pTr&&alive(pTr)&&objectName(componentGO(pTr))=="Road_01_Root"){
                            RoadVarRec rec{};
                            rec.go.set(a,go);
                            rec.name=nm;
                            road120026.vars.push_back(std::move(rec));
                        }
                    }
                }
            }
        }

        if(battleState!=5||road120026.vars.size()!=27){
            road120026.activeSolved=false;
            road120026.activeName.clear();
            road120026.activeGo.reset();
            espSummary="120026: chờ bắt đầu lượt chọn đường";
            return;
        }

        // Fast path: if the active Road_XX variant has already been solved and is still active,
        // reuse its cached markers in O(1) time (only 1 activeGO check, 0 hierarchy/collider scans!)
        Obj curActiveGo=road120026.activeGo.get();
        if(road120026.activeSolved&&curActiveGo&&alive(curActiveGo)&&activeGO(curActiveGo)){
            worldMarkers=road120026.cachedMarkers;
            espSummary=road120026.cachedSummary;
            return;
        }

        const RoadVarRec*selected=nullptr;
        for(const auto&v:road120026.vars){
            Obj g=v.go.get();
            if(g&&alive(g)&&activeGO(g)){
                if(selected){selected=nullptr;break;}
                selected=&v;
            }
        }
        if(!selected){espSummary="120026: chờ kích hoạt 1 phương án đường";return;}

        // Step 2: Fast targeted Transform DFS that tracks Road_line_1..3 down the tree
        // and only queries Collider components inside Road_line_1..3 subtrees (zero hierarchyNames calls!)
        Obj selGo=selected->go.get();
        Obj rootTr=getTransform(selGo);
        Method mChildCount=tryMethod(unity("Transform"),"get_childCount",0);
        Method mGetChild=tryMethod(unity("Transform"),"GetChild",1);
        Method mCompCount=tryMethod(unity("GameObject"),"GetComponentCount",0);
        Method mQueryComp=tryMethod(unity("GameObject"),"QueryComponentAtIndex",1);
        if(!rootTr||!mChildCount||!mGetChild||!mCompCount||!mQueryComp)return;

        std::map<int,std::vector<std::pair<Obj,Bounds>>> lanes;
        struct StackNode {Obj tr;int laneRow;};
        std::vector<StackNode> stack;
        stack.reserve(128);
        stack.push_back({rootTr,0});
        std::set<Obj> visited;

        while(!stack.empty()&&visited.size()<2048){
            auto[tr,laneRow]=stack.back();
            stack.pop_back();
            if(!tr||!alive(tr)||visited.contains(tr))continue;
            visited.insert(tr);

            Obj go=componentGO(tr);
            if(!go||!alive(go)||!activeGO(go))continue;

            int curRow=laneRow;
            if(curRow==0){
                std::string nm=objectName(go);
                if(nm.compare(0,10,"Road_line_")==0||nm.compare(0,10,"road_line_")==0){
                    if(nm.size()==11&&nm[10]>='1'&&nm[10]<='3')curRow=nm[10]-'0';
                    else if(nm.size()==12&&nm[10]=='0'&&nm[11]>='1'&&nm[11]<='3')curRow=nm[11]-'0';
                }
            }

            if(curRow>=1&&curRow<=3){
                int cCount=0;
                try{cCount=unbox<int>(invoke(mCompCount,go));}catch(...){cCount=0;}
                for(int i=0;i<cCount&&i<16;++i){
                    void*args[]={&i};
                    try{
                        Obj comp=invoke(mQueryComp,go,args);
                        if(!comp||!alive(comp))continue;
                        std::string t=classLabel(comp);
                        if(t!="BoxCollider"&&t!="MeshCollider")continue;
                        if(!colliderEnabled(comp)||colliderTrigger(comp))continue;
                        Bounds b=boundsOfCollider(comp);
                        if(b.extents.x<0.1f||b.extents.z<0.1f||b.extents.y>std::min(b.extents.x,b.extents.z))continue;
                        lanes[curRow].push_back({comp,b});
                    }catch(...){}
                }
            }

            int chCount=0;
            try{chCount=unbox<int>(invoke(mChildCount,tr));}catch(...){chCount=0;}
            for(int i=chCount-1;i>=0&&i<256;--i){
                void*args[]={&i};
                try{
                    Obj childTr=invoke(mGetChild,tr,args);
                    if(childTr&&alive(childTr))stack.push_back({childTr,curRow});
                }catch(...){}
            }
        }

        int verifiedRows=0;
        for(auto&[row,candidates]:lanes){
            std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){
                return a.second.extents.x*a.second.extents.z>b.second.extents.x*b.second.extents.z;
            });
            if(!candidates.empty()){
                const Bounds&b=candidates[0].second;
                ++verifiedRows;
                for(float frac:{-0.6f,0.f,0.6f}){
                    Vec3 pt{b.center.x,b.center.y+b.extents.y+0.04f,b.center.z+frac*b.extents.z};
                    const bool isCenter=(std::fabs(frac)<0.01f);
                    worldMarkers.push_back({pt,"green",isCenter?("H"+std::to_string(row)):std::string(),"road:"+std::to_string(row),row,true,isCenter,isCenter});
                }
            }
        }
        espSummary="120026 ("+selected->name+"): "+std::to_string(verifiedRows)+"/3 hàng sàn";
        if(verifiedRows>0){
            road120026.activeGo.set(a,selGo);
            road120026.activeName=selected->name;
            road120026.cachedMarkers=worldMarkers;
            road120026.cachedSummary=espSummary;
            road120026.activeSolved=true;
        }
    }

    // 3. Map 120006 (Map Cổng YES / NO - Map 3 Cửa + Line) - Cached once per round
    // Offsets from agent.js liveDoorTick (line 2435): manager +0x140, hideGameObjectList +0x38
    void collectDoors120006(Obj gameObj,int battleState,Vec3 playerPos){
        Obj manager=getHideObjectManager(gameObj);
        if(!manager){espSummary="120006: chờ HideObjectManager";return;}

        Obj hideList=nullptr;
        if(a.hasNativeLayout)safeReadAt(manager,0x38,hideList);
        if(!isValidObjectPointer(hideList))hideList=readObjFieldNamed(manager,{"hideGameObjectList","_hideGameObjectList"});
        std::int32_t listVer=readManagedListVersion(hideList);
        std::string rKey=std::to_string(reinterpret_cast<std::uintptr_t>(gameObj))+":"+
            std::to_string(reinterpret_cast<std::uintptr_t>(manager))+":"+std::to_string(listVer);
        if(doors120006.key!=rKey||!doors120006.ready){
            doors120006=Door120006State{};
            doors120006.key=rKey;
            auto objects=readManagedList(hideList,2048);
            if(objects.empty()){espSummary="120006: chờ danh sách cửa";return;}
            for(Obj go:objects){
                if(!go||!alive(go))continue;
                Obj tr=getTransform(go);
                if(!tr)continue;
                auto path=hierarchyNames(tr);
                DoorChoice ch{};
                if(!parseDoorIdentity(path,ch))continue;
                Vec3 anc{};
                try{anc=worldPos(tr);}catch(...){continue;}
                DoorObjRec rec{};
                rec.go.set(a,go);
                if(Obj pTr=getParent(tr)){
                    if(Obj pGo=componentGO(pTr))rec.parentGo.set(a,pGo);
                }
                rec.choice=ch;
                rec.anchor=anc;
                for(Obj c:getComponentsInChildrenByName(go,"Collider")){
                    if(c&&alive(c)){
                        Root cr(a,c);
                        rec.colliders.push_back(std::move(cr));
                    }
                }
                doors120006.items.push_back(std::move(rec));
            }
            doors120006.ready=!doors120006.items.empty();
        }
        if(!doors120006.ready){espSummary="120006: chờ danh sách cửa";return;}

        struct DoorRow {
            DoorChoice choice;
            Vec3 anchor{};
            bool active=false,parentActive=false,known=false,solid=false,suppress=false;
            int colliderCount=0;
            std::string color="yellow";
        };
        std::map<std::string,std::vector<DoorRow>> doorGroups;
        std::map<std::string,std::vector<DoorRow>> wallColGroups;

        for(auto&rec:doors120006.items){
            Obj go=rec.go.get();
            if(!go||!alive(go))continue;
            DoorRow row{};
            row.choice=rec.choice;
            row.anchor=rec.anchor;
            Obj pGo=rec.parentGo.get();
            row.parentActive=pGo&&alive(pGo)&&activeGO(pGo);
            row.active=activeGO(go);
            row.colliderCount=int(rec.colliders.size());
            row.solid=false;
            for(auto&cr:rec.colliders){
                Obj c=cr.get();
                if(c&&solidCollider(c)){row.solid=true;break;}
            }
            row.known=row.parentActive&&row.colliderCount>0;
            if(row.choice.kind=="door")doorGroups[row.choice.group].push_back(row);
            else wallColGroups[row.choice.group].push_back(row);
        }

        const bool playing=(battleState==5);
        std::vector<DoorRow> allDoors,allWallCols;

        for(auto&[grp,rows]:doorGroups){
            std::set<std::string> sides;
            bool allParent=true;
            for(const auto&r:rows){sides.insert(r.choice.side);if(!r.parentActive)allParent=false;}
            const bool complete=(rows.size()==3&&sides.size()==3&&allParent);
            int inactiveCount=0;bool collidersAgree=true;
            for(const auto&r:rows){
                if(!r.active)++inactiveCount;
                if(r.colliderCount>0&&r.solid!=r.active)collidersAgree=false;
            }
            if(playing&&complete&&inactiveCount==1&&collidersAgree){
                for(auto&r:rows){r.known=true;r.solid=r.active;r.color=r.active?"red":"green";}
            }else{
                bool allKnown=complete;int openCount=0;
                for(const auto&r:rows){if(!r.known)allKnown=false;else if(!r.solid)++openCount;}
                for(auto&r:rows)r.color=(playing&&allKnown&&openCount<=1)?(r.solid?"red":"green"):"yellow";
            }
            for(const auto&r:rows)allDoors.push_back(r);
        }

        for(auto&[grp,rows]:wallColGroups){
            std::set<std::string> sides;bool allParent=true;int activeCount=0;
            for(const auto&r:rows){
                sides.insert(r.choice.side);
                if(!r.parentActive)allParent=false;
                if(r.active)++activeCount;
            }
            const bool paired=(playing&&rows.size()==2&&sides.size()==2&&allParent&&activeCount==1);
            if(paired){
                if(rows[0].choice.kind=="column"){
                    for(auto&r:rows){
                        if(!r.active)r.suppress=true;
                        else{
                            bool pass=(r.choice.side=="through");
                            r.color=pass?"green":"red";
                            r.solid=!pass;
                        }
                    }
                }else{
                    for(auto&r:rows){
                        r.color=r.active?"red":"green";
                        r.solid=r.active;
                    }
                }
            }else{
                for(auto&r:rows){
                    if(r.known)r.color=r.solid?"red":"green";
                    else r.color="yellow";
                }
            }
            for(const auto&r:rows)allWallCols.push_back(r);
        }

        int yesCount=0,noCount=0;
        auto emitDoorMarker=[&](const DoorRow&r){
            if(r.suppress)return;
            std::string lbl=r.color=="green"?"YES":(r.color=="red"?"NO":"CHƯA RÕ");
            if(r.color=="green")++yesCount;else if(r.color=="red")++noCount;
            worldMarkers.push_back({r.anchor,r.color,lbl,r.choice.group+":"+r.choice.side,0,r.color=="green",true,true});
        };
        for(const auto&r:allDoors)emitDoorMarker(r);
        for(const auto&r:allWallCols)emitDoorMarker(r);

        struct Gate {std::string key;Vec3 center{},anchor{};bool passable=false;};
        std::vector<Gate> gates;
        auto makeGate=[&](const std::string&key,const std::vector<DoorRow>&group,std::size_t expectedSize)->bool{
            if(group.size()!=expectedSize)return false;
            Vec3 c{0,0,0};
            const DoorRow*openRow=nullptr;int openCnt=0;
            for(const auto&r:group){
                c.x+=r.anchor.x;c.y+=r.anchor.y;c.z+=r.anchor.z;
                if(r.color=="green"&&!r.solid){openRow=&r;++openCnt;}
            }
            c.x/=float(group.size());c.y/=float(group.size());c.z/=float(group.size());
            bool pass=(openCnt==1&&openRow!=nullptr);
            gates.push_back({key,c,pass?openRow->anchor:c,pass});
            return true;
        };

        bool gatesOk=playing&&valid(playerPos);
        for(int n=1;n<=4&&gatesOk;++n){
            std::string k="wall"+std::to_string(n);
            gatesOk=makeGate(k,wallColGroups[k],2);
        }
        if(gatesOk){
            const auto&cols=wallColGroups["entrance-column"];
            if(cols.size()==2){
                Vec3 c{(cols[0].anchor.x+cols[1].anchor.x)*0.5f,(cols[0].anchor.y+cols[1].anchor.y)*0.5f,(cols[0].anchor.z+cols[1].anchor.z)*0.5f};
                const DoorRow*act=cols[0].active?&cols[0]:(cols[1].active?&cols[1]:nullptr);
                bool pass=(act&&act->color=="green"&&!act->solid);
                gates.push_back({"entrance-column",c,act?act->anchor:c,pass});
            }else gatesOk=false;
        }
        if(gatesOk)gatesOk=makeGate("door1",doorGroups["door1"],3);

        if(gatesOk&&gates.size()==6){
            float dx=gates.back().center.x-gates.front().center.x;
            float dz=gates.back().center.z-gates.front().center.z;
            bool useX=std::fabs(dx)>std::fabs(dz);
            float delta=useX?dx:dz;
            float sign=delta>0.f?1.f:(delta<0.f?-1.f:0.f);
            if(sign!=0.f&&std::max(std::fabs(dx),std::fabs(dz))>=5.f){
                auto coord=[&](Vec3 p){return sign*(useX?p.x:p.z);};
                std::sort(gates.begin(),gates.end(),[&](const Gate&a,const Gate&b){return coord(a.center)<coord(b.center);});
                float progress=coord(playerPos);
                routeNodes.push_back({{playerPos.x,playerPos.y+0.05f,playerPos.z},"@player",false,0,false});
                auto appendNode=[&](const Gate&g,float offset,const char*suffix){
                    Vec3 a=g.anchor;
                    if(useX)a.x+=sign*offset;else a.z+=sign*offset;
                    a.y+=0.2f;
                    if(distXZ(routeNodes.back().anchor,a)>0.05f){
                        routeNodes.push_back({a,g.key+":"+suffix,false,0,false});
                    }
                };
                constexpr float approach=2.f;
                for(const auto&g:gates){
                    if(progress>coord(g.center)+approach)continue;
                    if(!g.passable){
                        if(progress<coord(g.center)-approach)appendNode(g,-approach,"stop");
                        break;
                    }
                    if(progress<coord(g.anchor)-approach)appendNode(g,-approach,"approach");
                    if(progress<=coord(g.anchor))appendNode(g,0.f,"pass");
                    appendNode(g,approach,"exit");
                }
            }
        }
        currentRouteStage=0;
        espSummary="120006 (Cổng YES/NO): "+std::to_string(yesCount)+" YES, "+std::to_string(noCount)+" NO";
    }

    // 4. Map 120015 (Map Tìm Lỗ + Line) - Hiển thị TẤT CẢ các lỗ rớt xuống được màu xanh lá + Line đa tầng (không lag!)
    // Exact offsets verified from dump.cs & agent.js:
    // - MiniGameKMGUnit.hideObjectManager: +0x140 (HideObjectManager)
    // - MiniGameKMGTwistRoad.endPointGo: +0x1D8 (GameObject)
    // - HideObjectManager.hideStampGameObjectList: +0x40 (List<HideStampObjectData>)
    // - HideObjectManager.BoardRootTransform: +0xA8 (Transform)
    // - HideObjectManager.EndPoint: +0xB8 (GameObject)
    // - HideStampObjectData: uniqueId +0x20, ObjectRenderer +0x30, <ObjectCollider>k__BackingField +0x40,
    //   StableStateObject +0x48, _curHideState +0x58 (int32), _isScaleTween +0x90 (uint8),
    //   _originLocalPos +0xAC (Vector3), _originLocalScale +0xB8 (Vector3), _maxStep +0xE0 (int32), _curStep +0xE4 (int32)
    bool resolveHole120015Anchor(HoleTileRec&rec,Obj obj,int state){
        if(rec.hasCachedPos)return true;
        Obj t=rec.tr.get();
        if(!t||!alive(t))t=getTransform(obj);
        if(!rec.nameChecked){
            rec.nameChecked=true;
            Obj go=componentGO(obj);
            if(go&&alive(go)){
                rec.stampId=parseHoleStampId(objectName(go));
                if(rec.stampId>=1&&rec.stampId<=155){
                    rec.stage=int(kHoleStampRefs[rec.stampId].stage);
                }
            }
        }
        auto acceptBounds=[&](const Bounds&b)->bool{
            if(!validBounds(b)||b.extents.x<=0.05f||b.extents.z<=0.05f||b.extents.y>std::min(b.extents.x,b.extents.z))return false;
            if(rec.stampId>0&&std::fabs(b.center.x)<0.001f&&std::fabs(b.center.z)<0.001f)return false;
            rec.cachedPos={b.center.x,b.center.y+b.extents.y,b.center.z};
            rec.hasCachedPos=true;
            return true;
        };
        // 1. Live solid Collider.bounds when state == 0 (intact cover before any scale/fall tween)
        Obj col=getStampCollider(obj);
        if(state==0&&col&&alive(col)&&solidCollider(col)){
            try{
                Bounds b=boundsOfCollider(col);
                if(acceptBounds(b)){
                    if(rec.stage<=0)rec.stage=std::clamp(int(std::round((81.f-rec.cachedPos.y)/10.f)),1,7);
                    return true;
                }
            }catch(...){}
        }
        // 2. Pre-measured 155-stamp mesh geometry table from agent.js measuredHoleReferences
        //    Use the stationary board origin (0, 80 - stage*10, 0) or Stage_Board_0X_v Transform so tweening tiles never shift!
        if(rec.stampId>=1&&rec.stampId<=155){
            const auto&ref=kHoleStampRefs[rec.stampId];
            Vec3 wp{0.f,float(80-int(ref.stage)*10),0.f};
            Obj bGo=rec.boardGo.get();
            if(bGo&&alive(bGo)){
                if(Obj bTr=getTransform(bGo)){
                    try{
                        Vec3 bWp=worldPos(bTr);
                        if(valid(bWp)&&std::fabs(bWp.x)<5.f&&std::fabs(bWp.z)<5.f)wp=bWp;
                    }catch(...){}
                }
            }
            rec.cachedPos={wp.x+ref.dx,wp.y+1.0f,wp.z+ref.dz};
            rec.stage=int(ref.stage);
            rec.hasCachedPos=true;
            return true;
        }
        // 3. Fallbacks for synthetic/non-standard stamps (Renderer.bounds -> inactiveHoleBounds -> worldPos)
        Obj ren=nullptr;
        if(a.hasNativeLayout)safeReadAt(obj,0x30,ren);
        if(!ren||!alive(ren))ren=readObjFieldNamed(obj,{"ObjectRenderer","_renderer","renderer","meshRenderer"});
        if(ren&&alive(ren)){
            try{
                Bounds b=unbox<Bounds>(call(ren,"get_bounds"));
                if(acceptBounds(b)){
                    if(rec.stage<=0)rec.stage=std::clamp(int(std::round((81.f-rec.cachedPos.y)/10.f)),1,7);
                    return true;
                }
            }catch(...){}
        }
        if(col&&alive(col)){
            Bounds ib{};
            if(inactiveHoleBounds(col,ib)&&acceptBounds(ib)){
                if(rec.stage<=0)rec.stage=std::clamp(int(std::round((81.f-rec.cachedPos.y)/10.f)),1,7);
                return true;
            }
        }
        if(t&&alive(t)){
            try{
                Vec3 wp=worldPos(t);
                if(valid(wp)){
                    rec.cachedPos=wp;
                    if(rec.stage<=0)rec.stage=std::clamp(int(std::round((81.f-wp.y)/10.f)),1,7);
                    rec.hasCachedPos=true;
                    return true;
                }
            }catch(...){}
        }
        return false;
    }

    void collectHoles120015(Obj gameObj,int battleState,Vec3 playerPos){
        (void)battleState;
        Obj manager=getHideObjectManager(gameObj);
        if(!manager){espSummary="120015: chờ HideObjectManager";return;}
        Obj stampListObj=nullptr;
        if(a.hasNativeLayout){
            safeReadAt(manager,0x40,stampListObj);
        }
        if(!isValidObjectPointer(stampListObj)){
            stampListObj=readObjFieldNamed(manager,{"hideStampObjectDataList","_hideStampObjectDataList","hideStampGameObjectList","_hideStampGameObjectList"});
        }
        auto registeredVec=readManagedList(stampListObj,2048);
        if(registeredVec.empty()){
            Obj rootGo=topAncestorGO(gameObj);
            if(rootGo)registeredVec=getComponentsInChildrenByName(rootGo,"HideStampObjectData");
        }
        if(registeredVec.empty()){espSummary="120015: chờ danh sách lỗ";return;}

        // Cache all HideStampObjectData tiles + their parent Stage_Board_0X_v GameObject,
        // keyed on gameObj, manager, stampListObj, list._version (+0x1C), size, and endpoints.
        // Do NOT filter by _maxStep during cache construction because the server sets _maxStep (+0xE0) when the round starts!
        std::int32_t listVer=readManagedListVersion(stampListObj);
        std::uintptr_t firstPtr=reinterpret_cast<std::uintptr_t>(registeredVec.front());
        std::uintptr_t lastPtr=reinterpret_cast<std::uintptr_t>(registeredVec.back());
        std::string roundKey=std::to_string(reinterpret_cast<std::uintptr_t>(gameObj))+":"+
            std::to_string(reinterpret_cast<std::uintptr_t>(manager))+":"+
            std::to_string(reinterpret_cast<std::uintptr_t>(stampListObj))+":"+
            std::to_string(listVer)+":"+
            std::to_string(registeredVec.size())+":"+
            std::to_string(firstPtr)+":"+std::to_string(lastPtr);
        if(cachedRoundKey!=roundKey||!holeStructureReady){
            holeTiles.clear();holeSavedAnchors.clear();
            cachedRoundKey=roundKey;
            holeTiles.reserve(registeredVec.size());
            std::set<Obj> seenStamps;
            auto addStamp=[&](Obj raw){
                if(!raw||!alive(raw))return;
                Obj s=raw;
                if(classLabel(raw)=="GameObject"){
                    auto comps=getComponentsInChildrenByName(raw,"HideStampObjectData");
                    if(!comps.empty())s=comps.front();
                }
                if(!s||!alive(s)||seenStamps.contains(s))return;
                seenStamps.insert(s);

                Obj t=getTransform(s);
                if(!t||!alive(t))return;

                // Walk up parent chain to locate Stage_Board_0X_v (fallback to p2 -> p1 -> self for synthetic tests)
                Obj p1=getParent(t);
                Obj p2=p1?getParent(p1):nullptr;
                Obj bGo=nullptr;
                for(Obj cur=p1;cur&&alive(cur);cur=getParent(cur)){
                    Obj cg=componentGO(cur);
                    if(cg&&alive(cg)){
                        std::string nm=objectName(cg);
                        if(nm.compare(0,12,"Stage_Board_")==0){
                            bGo=cg;
                            break;
                        }
                    }
                }
                if(!bGo)bGo=p2?componentGO(p2):(p1?componentGO(p1):componentGO(s));

                HoleTileRec rec{};
                rec.comp.set(a,s);
                rec.tr.set(a,t);
                if(bGo&&alive(bGo))rec.boardGo.set(a,bGo);
                holeTiles.push_back(std::move(rec));
            };
            for(Obj raw:registeredVec)addStamp(raw);
            holeStructureReady=!holeTiles.empty();
        }
        if(!holeStructureReady){espSummary="120015: chờ dữ liệu ô";return;}

        // Deduplicate active checks on Stage_Board_XX_x (at most 35 board variants total -> super fast!)
        std::map<Obj,bool> boardActiveCache;
        auto isBoardActive=[&](Obj bGo)->bool{
            if(!bGo)return false;
            if(auto it=boardActiveCache.find(bGo);it!=boardActiveCache.end())return it->second;
            bool act=alive(bGo)&&activeGO(bGo);
            boardActiveCache[bGo]=act;
            return act;
        };

        struct HoleCand {std::string key;Vec3 anchor{};int stage=0;float dist=0.f;};
        std::vector<HoleCand> allDropHoles;
        allDropHoles.reserve(36);

        for(auto&rec:holeTiles){
            Obj obj=rec.comp.get();
            if(!obj||!alive(obj))continue;

            // 1. Must belong to the active Stage_Board_0X_v variant of its floor
            Obj bGo=rec.boardGo.get();
            if(bGo){
                if(!isBoardActive(bGo))continue;
            }else{
                Obj go=componentGO(obj);
                if(!activeGO(go))continue;
            }

            // 2. Check _maxStep (+0xE0) LIVE every tick (agent.js collectLiveHoles line 1222: maxStep === 0)
            std::int32_t maxStep=0;
            if(!readFieldNamed<std::int32_t>(obj,{"_maxStep","maxStep"},maxStep)&&a.hasNativeLayout){
                safeReadAt(obj,0xE0,maxStep);
            }
            if(maxStep!=0)continue;

            // 3. Check _curHideState (+0x58) LIVE every tick (agent.js collectLiveHoles line 1244):
            //    State 0 = intact false cover that drops when stepped on (must have solid collider if present)
            //    State 1..3 = opening / already-opened hole you can drop right through
            //    State 4, 5 = non-drop / Stable solid block -> exclude!
            int state=getStampState(obj);
            if(state==4||state==5||(a.hasNativeLayout&&(state<0||state>3)))continue;
            if(state==0){
                Obj col=getStampCollider(obj);
                if(col&&alive(col)&&!solidCollider(col))continue;
            }

            if(!resolveHole120015Anchor(rec,obj,state))continue;
            if(state<0&&rec.stampId==0)continue;

            // Skip floors already far above the player's head (floors are 10m apart: 71, 61, 51, 41, 31, 21, 11)
            if(valid(playerPos)&&rec.cachedPos.y>playerPos.y+5.5f)continue;

            Vec3 anchor{rec.cachedPos.x,rec.cachedPos.y+0.04f,rec.cachedPos.z};
            int st=rec.stage>0?rec.stage:std::clamp(int(std::round((81.f-rec.cachedPos.y)/10.f)),1,7);
            std::string key=std::to_string(reinterpret_cast<std::uintptr_t>(obj));
            allDropHoles.push_back({key,anchor,st,valid(playerPos)?distXZ(playerPos,anchor):0.f});
        }

        // Goal marker at bottom floor (Final_Board)
        Obj endpointGo=readObjFieldNamed(gameObj,{"endPointGo","_endPointGo","EndPointGo","endPoint","_endPoint","EndPoint"});
        if((!endpointGo||!alive(endpointGo))&&a.hasNativeLayout){
            safeReadAt(gameObj,0x1D8,endpointGo);
        }
        if(!endpointGo||!alive(endpointGo)){
            endpointGo=readObjFieldNamed(manager,{"EndPoint","endPoint","endPointGo","_endPoint"});
            if((!endpointGo||!alive(endpointGo))&&a.hasNativeLayout){
                safeReadAt(manager,0xB8,endpointGo);
            }
        }
        Vec3 goalPos{};
        bool hasGoal=false;
        if(endpointGo&&alive(endpointGo)&&activeGO(endpointGo)){
            try{
                goalPos=worldPos(getTransform(endpointGo));
                hasGoal=valid(goalPos);
            }catch(...){}
        }

        // Group remaining drop holes by floor stage (ordered from highest floor to lowest floor)
        struct FloorLayer {int stage=0;float y=0.f;std::vector<HoleCand> holes;};
        std::vector<FloorLayer> layers;
        for(int st=1;st<=7;++st){
            FloorLayer layer{st,0.f,{}};
            for(const auto&h:allDropHoles){
                if(h.stage==st)layer.holes.push_back(h);
            }
            if(!layer.holes.empty()){
                layer.y=layer.holes.front().anchor.y;
                layers.push_back(std::move(layer));
            }
        }
        std::sort(layers.begin(),layers.end(),[](const FloorLayer&a,const FloorLayer&b){return a.y>b.y;});

        // Backward Dynamic Programming across remaining floors to find the shortest multi-floor drop path
        std::vector<HoleCand> chosenPath;
        std::set<std::string> chosenKeys;
        std::string currentBestKey;
        if(!layers.empty()){
            const std::size_t L=layers.size();
            std::vector<std::vector<float>> dpCost(L);
            std::vector<std::vector<int>> dpNext(L);
            for(std::size_t k=0;k<L;++k){
                dpCost[k].assign(layers[k].holes.size(),0.f);
                dpNext[k].assign(layers[k].holes.size(),-1);
            }
            for(std::size_t j=0;j<layers[L-1].holes.size();++j){
                dpCost[L-1][j]=hasGoal?distXZ(layers[L-1].holes[j].anchor,goalPos):0.f;
            }
            for(int k=int(L)-2;k>=0;--k){
                const auto&curH=layers[std::size_t(k)].holes;
                const auto&nxtH=layers[std::size_t(k+1)].holes;
                for(std::size_t i=0;i<curH.size();++i){
                    float bestC=1e30f;int bestJ=0;
                    for(std::size_t j=0;j<nxtH.size();++j){
                        float c=distXZ(curH[i].anchor,nxtH[j].anchor)+dpCost[std::size_t(k+1)][j];
                        if(c<bestC){bestC=c;bestJ=int(j);}
                    }
                    dpCost[std::size_t(k)][i]=bestC;
                    dpNext[std::size_t(k)][i]=bestJ;
                }
            }
            int bestStartIdx=0;
            float bestTotal=1e30f;
            for(std::size_t i=0;i<layers[0].holes.size();++i){
                float c=(valid(playerPos)?distXZ(playerPos,layers[0].holes[i].anchor):0.f)+dpCost[0][i];
                if(c<bestTotal){bestTotal=c;bestStartIdx=int(i);}
            }
            int curIdx=bestStartIdx;
            for(std::size_t k=0;k<L&&curIdx>=0;++k){
                const auto&h=layers[k].holes[std::size_t(curIdx)];
                chosenPath.push_back(h);
                chosenKeys.insert(h.key);
                curIdx=dpNext[k][std::size_t(curIdx)];
            }
            if(!chosenPath.empty())currentBestKey=chosenPath.front().key;
        }

        // Emit green ESP for ALL drop-able holes!
        for(const auto&h:allDropHoles){
            const bool isCurrentBest=(h.key==currentBestKey);
            const bool isRouteHole=chosenKeys.contains(h.key);
            std::string label=isCurrentBest?"LỖ RỚT":(isRouteHole?("LỖ T"+std::to_string(h.stage)):"LỖ");
            worldMarkers.push_back({h.anchor,"green",label,h.key,h.stage,isCurrentBest,true,isCurrentBest});
        }

        // Build multi-floor guiding Line from player's feet through each floor's optimal hole down to ĐÍCH
        if(valid(playerPos)&&!chosenPath.empty()){
            currentRouteStage=chosenPath.front().stage;
            routeNodes.push_back({{playerPos.x,playerPos.y+0.04f,playerPos.z},"@feet",false,currentRouteStage,false});
            Vec3 prev=playerPos;
            for(std::size_t k=0;k<chosenPath.size();++k){
                const auto&h=chosenPath[k];
                const float surfaceY=h.anchor.y+0.04f;
                if(k>0||prev.y>h.anchor.y+3.0f){
                    routeNodes.push_back({{prev.x,surfaceY,prev.z},"@land:"+std::to_string(h.stage),true,h.stage,false});
                }
                routeNodes.push_back({{h.anchor.x,surfaceY,h.anchor.z},h.key,false,h.stage,false});
                prev=h.anchor;
            }
            if(hasGoal&&goalPos.y<chosenPath.back().anchor.y-1.0f){
                const float goalY=goalPos.y+0.04f;
                routeNodes.push_back({{prev.x,goalY,prev.z},"@land:goal",true,8,false});
                routeNodes.push_back({{goalPos.x,goalY,goalPos.z},"@goal",false,8,true});
            }
        }

        if(hasGoal){
            worldMarkers.push_back({goalPos,"green","ĐÍCH","@goal",8,true,true,true});
            if(chosenPath.empty()&&valid(playerPos)){
                currentRouteStage=8;
                routeNodes.clear();
                routeNodes.push_back({{playerPos.x,playerPos.y+0.04f,playerPos.z},"@feet",false,8,false});
                routeNodes.push_back({{goalPos.x,goalPos.y+0.04f,goalPos.z},"@goal",false,8,true});
            }
        }

        espSummary="120015 (Map Lỗ): "+std::to_string(allDropHoles.size())+" lỗ rớt ("+std::to_string(layers.size())+" tầng)";
    }

    void readPlayerCollision(Obj actorObj,float&outRadius,float&outHeight,std::uint32_t&outMask){
        outRadius=0.25f;
        outHeight=1.05f;
        outMask=0xFFFFFFFFu;
        Obj ctrl=currentControl(actorObj);
        if(!isValidObjectPointer(ctrl))return;
        Obj unit=readObjFieldNamed(ctrl,{"unitController","_unitController","<UnitController>k__BackingField"});
        if(!isValidObjectPointer(unit)&&a.hasNativeLayout)safeReadAt(ctrl,0xA0,unit);
        if(!isValidObjectPointer(unit))return;
        Obj motor=readObjFieldNamed(unit,{"Motor","_motor","motor"});
        if(!isValidObjectPointer(motor)&&a.hasNativeLayout)safeReadAt(unit,0x20,motor);
        if(!isValidObjectPointer(motor))return;
        float r=0.f,h=0.f;
        std::int32_t mask=0;
        if(!readFieldNamed<float>(motor,{"CapsuleRadius","_capsuleRadius"},r)&&a.hasNativeLayout){
            safeReadAt(motor,0x28,r);
        }
        if(!readFieldNamed<float>(motor,{"CapsuleHeight","_capsuleHeight"},h)&&a.hasNativeLayout){
            safeReadAt(motor,0x2C,h);
        }
        if(!readFieldNamed<std::int32_t>(motor,{"CollidableLayers"},mask)&&a.hasNativeLayout){
            safeReadAt(motor,0xF0,mask);
        }
        if(std::isfinite(r)&&r>=0.1f&&r<=0.45f)outRadius=r;
        if(std::isfinite(h)&&h>=0.5f&&h<=2.5f)outHeight=h;
        if(mask!=0)outMask=static_cast<std::uint32_t>(mask);
    }

    // 5. Map 120041 (MiniGameKMGFogMaze - Đích Mê Cung + Line A*8)
    // Exact dump.cs & agent.js (mazeFixedEndpoints / solveMazeGoalPortal / solveMazeGrid):
    // `public FowManager FogManager; // +0x1D8` (`StartingPoint` +0x78, `EndingPoint` +0x80)
    // `public GameObject EndPointGo; // +0x1E0`
    // Interior maze walls live on `RandomMaze_Root/MazeCreate_S_XX_D/maze_stage_XX/maze_stage_XX_Collider` (MeshCollider),
    // while outer boundary walls, start room walls, temporary gates (`ControlCollider/Start_Wall`, `Finish_Wall`),
    // and goal doorway frame (`Finish_Wall_Collider`) are BoxColliders under `MapRoot`.
    void collectMaze120041(Obj actorObj,Obj gameObj,int battleState,Vec3 playerPos){
        Obj fowManager=readObjFieldNamed(gameObj,{"FogManager","_fowManager","fowManager"});
        if((!fowManager||!alive(fowManager))&&a.hasNativeLayout)safeReadAt(gameObj,0x1D8,fowManager);
        Obj endpointGo=readObjFieldNamed(gameObj,{"EndPointGo","endPointGo","_endPointGo"});
        if((!endpointGo||!alive(endpointGo))&&a.hasNativeLayout)safeReadAt(gameObj,0x1E0,endpointGo);
        Obj manager=getHideObjectManager(gameObj);
        if((!endpointGo||!alive(endpointGo))&&manager){
            endpointGo=readObjFieldNamed(manager,{"EndPoint","endPoint","EndPointGo"});
            if((!endpointGo||!alive(endpointGo))&&a.hasNativeLayout)safeReadAt(manager,0xB8,endpointGo);
        }
        Vec3 goalPos{};bool hasGoal=false;
        if(endpointGo&&alive(endpointGo)){
            try{goalPos=worldPos(getTransform(endpointGo));hasGoal=valid(goalPos);}catch(...){}
        }
        if(!hasGoal&&fowManager&&alive(fowManager)){
            Obj endTr=readObjFieldNamed(fowManager,{"EndingPoint","_endingPoint","endingPoint"});
            if((!endTr||!alive(endTr))&&a.hasNativeLayout)safeReadAt(fowManager,0x80,endTr);
            if(endTr&&alive(endTr)){
                try{goalPos=worldPos(endTr);hasGoal=valid(goalPos);}catch(...){}
            }
        }
        if(!hasGoal){
            espSummary="120041: chờ cổng đích mê cung";return;
        }

        worldMarkers.push_back({goalPos,"green","ĐÍCH","@maze_goal",0,true,true,true});

        Vec3 startPos=playerPos;
        if(fowManager&&alive(fowManager)){
            Obj startTr=readObjFieldNamed(fowManager,{"StartingPoint","_startingPoint","startingPoint"});
            if((!startTr||!alive(startTr))&&a.hasNativeLayout)safeReadAt(fowManager,0x78,startTr);
            if(startTr&&alive(startTr)){
                try{Vec3 sp=worldPos(startTr);if(valid(sp))startPos=sp;}catch(...){}
            }
        }

        const std::string roundSuffix=(battleState>=6&&battleState<=10)?" • hết lượt":"";
        std::string mapKey=std::to_string(reinterpret_cast<std::uintptr_t>(gameObj));

        // 1. Scan full MapRoot hierarchy once per map instance to collect all MeshCollider & BoxCollider candidates
        if(maze.key!=mapKey||!maze.hierarchyScanned){
            maze=MazeState{};
            maze.key=mapKey;
            Obj scanRoot=topAncestorGO(gameObj);
            if(!scanRoot||!alive(scanRoot))scanRoot=topAncestorGO(endpointGo);
            if(!scanRoot||!alive(scanRoot))scanRoot=topAncestorGO(fowManager);
            if(!scanRoot||!alive(scanRoot))scanRoot=componentGO(gameObj);
            for(Obj c:getComponentsInChildrenByName(scanRoot,"Collider")){
                if(!c||!alive(c))continue;
                std::string t=classLabel(c);
                Obj go=componentGO(c);
                if(!go||!alive(go))continue;
                auto path=hierarchyNames(getTransform(go));
                std::string var;
                parseMazeVariant(path,var);
                if(t=="MeshCollider"){
                    if(!var.empty()){
                        MazeMeshCand mc{};
                        mc.col.set(a,c);
                        mc.go.set(a,go);
                        mc.variant=var;
                        maze.meshCandidates.push_back(std::move(mc));
                    }
                }else if(t=="BoxCollider"){
                    MazeBoxRole role=MazeBoxRole::WallBox;
                    if(path.size()>=2&&path[path.size()-2]=="Default_board"&&path.back()=="Board"){
                        role=MazeBoxRole::Floor;
                    }else if(std::find(path.begin(),path.end(),"ControlCollider")!=path.end()&&!path.empty()&&path.back()=="Start_Wall"){
                        role=MazeBoxRole::StartGate;
                    }else if(std::find(path.begin(),path.end(),"ControlCollider")!=path.end()&&!path.empty()&&path.back()=="Finish_Wall"){
                        role=MazeBoxRole::FinishGate;
                    }else if(std::find(path.begin(),path.end(),"Finish_Wall_Collider")!=path.end()){
                        role=MazeBoxRole::GoalFrame;
                    }
                    MazeBoxCand bc{};
                    bc.col.set(a,c);
                    bc.go.set(a,go);
                    bc.variant=var;
                    bc.role=role;
                    maze.boxCandidates.push_back(std::move(bc));
                }
            }
            maze.hierarchyScanned=true;
        }

        // 2. Detect currently active MazeCreate_S_XX_D variant and its MeshCollider
        std::string detectedVariant;
        Obj detectedMeshCol=nullptr;
        for(const auto&mc:maze.meshCandidates){
            Obj c=mc.col.get();
            if(c&&alive(c)&&solidCollider(c)){
                detectedVariant=mc.variant;
                detectedMeshCol=c;
                break;
            }
        }
        if(detectedVariant.empty()&&manager){
            Obj hideList=nullptr;
            if(a.hasNativeLayout)safeReadAt(manager,0x38,hideList);
            if(!isValidObjectPointer(hideList))hideList=readObjFieldNamed(manager,{"hideGameObjectList","_hideGameObjectList"});
            for(Obj go:readManagedList(hideList,2048)){
                if(!go||!alive(go))continue;
                std::string nm=objectName(go);
                if(nm.size()==17&&nm.compare(0,13,"MazeCreate_S_")==0&&activeGO(go)){
                    detectedVariant=nm;
                    break;
                }
            }
            if(!detectedVariant.empty()&&!detectedMeshCol){
                for(const auto&mc:maze.meshCandidates){
                    if(mc.variant==detectedVariant){
                        Obj c=mc.col.get();
                        if(c&&alive(c)&&solidCollider(c)){detectedMeshCol=c;break;}
                    }
                }
            }
        }
        if(detectedVariant.empty()){
            for(const auto&bc:maze.boxCandidates){
                if(!bc.variant.empty()){
                    Obj c=bc.col.get();
                    if(c&&alive(c)&&solidCollider(c)){detectedVariant=bc.variant;break;}
                }
            }
        }

        // 3. Read player collision & active BoxColliders first so floorB (floorTop) is known for raycasting
        float bodyRadius=0.25f,bodyHeight=1.05f;
        std::uint32_t collidableMask=0xFFFFFFFFu;
        readPlayerCollision(actorObj,bodyRadius,bodyHeight,collidableMask);

        Bounds floorB{Vec3{(startPos.x+goalPos.x)*0.5f,std::min(startPos.y,goalPos.y)-0.5f,(startPos.z+goalPos.z)*0.5f},Vec3{60.f,0.5f,60.f}};
        bool hasExactFloor=false;
        std::vector<Bounds> allSolidBoxes;
        std::vector<Bounds> staticBoxes;
        std::vector<Bounds> goalWalls;
        std::vector<MazeGateRec> gates;

        const std::string filterVar=!detectedVariant.empty()?detectedVariant:maze.activeVariant;
        for(const auto&bc:maze.boxCandidates){
            Obj c=bc.col.get();
            if(!c||!alive(c)||!solidCollider(c))continue;
            if(!bc.variant.empty()&&!filterVar.empty()&&bc.variant!=filterVar)continue;
            Obj go=bc.go.get();
            int layer=objectLayer(go);
            if(layer>=0&&layer<32&&((collidableMask&(1u<<layer))==0))continue;
            try{
                Bounds b=boundsOfCollider(c);
                if(bc.role==MazeBoxRole::Floor){
                    floorB=b;
                    hasExactFloor=true;
                }else if(!hasExactFloor&&b.extents.x>12.f&&b.extents.z>12.f&&b.extents.y<1.2f){
                    floorB=b;
                }else{
                    allSolidBoxes.push_back(b);
                    if(bc.role!=MazeBoxRole::StartGate&&bc.role!=MazeBoxRole::FinishGate){
                        staticBoxes.push_back(b);
                    }
                    if(bc.role==MazeBoxRole::GoalFrame){
                        goalWalls.push_back(b);
                    }
                    if(bc.role==MazeBoxRole::StartGate||bc.role==MazeBoxRole::FinishGate){
                        gates.push_back({bc.role==MazeBoxRole::StartGate,bc.role==MazeBoxRole::FinishGate,true,b});
                    }
                }
            }catch(...){}
        }
        const float floorTop=floorB.center.y+floorB.extents.y;

        auto loadPresetGrid=[&](const std::string&varName)->bool{
            if(varName.empty())return false;
            for(const auto&preset:kMazePresetGrids){
                if(varName==preset.variant){
                    maze.meshStep=0.5f;
                    maze.meshW=preset.w;
                    maze.meshH=preset.h;
                    maze.meshX0=preset.x0;
                    maze.meshZ0=preset.z0;
                    maze.meshHeights.assign(std::size_t(preset.w*preset.h),std::numeric_limits<float>::quiet_NaN());
                    maze.wallHitCount=0;
                    for(int k=0;k<preset.w*preset.h;++k){
                        if((preset.bits[k>>3]&(1u<<(k&7)))!=0){
                            maze.meshHeights[std::size_t(k)]=floorTop+7.45f;
                            ++maze.wallHitCount;
                        }
                    }
                    maze.nextRayIdx=preset.w*preset.h;
                    maze.gridComplete=true;
                    return true;
                }
            }
            return false;
        };

        // Reset raycast grid when active variant or active MeshCollider changes
        if(detectedMeshCol){
            if(detectedVariant!=maze.activeVariant||detectedMeshCol!=maze.activeMeshCol.get()){
                maze.activeVariant=detectedVariant;
                maze.activeMeshCol.set(a,detectedMeshCol);
                try{maze.meshBounds=boundsOfCollider(detectedMeshCol);}catch(...){maze.meshBounds={};}
                maze.meshStep=0.5f;
                maze.wallHitCount=0;
                maze.raycastBackendMode=0;
                // On full-size 70x70 game mazes (extents > 14m), immediately load verified preset bitmap if available (0ms instant solve!)
                if(maze.meshBounds.extents.x>14.f&&maze.meshBounds.extents.z>14.f&&loadPresetGrid(maze.activeVariant)){
                    // Preset loaded immediately on Frame 1
                }else if(maze.meshBounds.extents.x>0.5f&&maze.meshBounds.extents.z>0.5f){
                    maze.meshX0=maze.meshBounds.center.x-maze.meshBounds.extents.x;
                    maze.meshZ0=maze.meshBounds.center.z-maze.meshBounds.extents.z;
                    float xmax=maze.meshBounds.center.x+maze.meshBounds.extents.x;
                    float zmax=maze.meshBounds.center.z+maze.meshBounds.extents.z;
                    maze.meshW=int(std::ceil((xmax-maze.meshX0)/maze.meshStep))+1;
                    maze.meshH=int(std::ceil((zmax-maze.meshZ0)/maze.meshStep))+1;
                    if(maze.meshW>=2&&maze.meshH>=2&&maze.meshW*maze.meshH<=25000){
                        maze.meshHeights.assign(std::size_t(maze.meshW*maze.meshH),std::numeric_limits<float>::quiet_NaN());
                        maze.nextRayIdx=0;
                        maze.gridComplete=false;
                    }else{
                        maze.meshW=0;maze.meshH=0;maze.meshHeights.clear();maze.gridComplete=true;
                    }
                }else{
                    maze.meshW=0;maze.meshH=0;maze.meshHeights.clear();maze.gridComplete=true;
                }
                maze.routeReady=false;
                maze.routeSignature.clear();
                maze.lastPlanOrigin={1e30f,1e30f,1e30f};
                maze.solvedNodes.clear();
                maze.solvedObstacles.clear();
            }
        }else if(!detectedVariant.empty()&&detectedVariant!=maze.activeVariant){
            maze.activeVariant=detectedVariant;
            if(!loadPresetGrid(maze.activeVariant)){
                maze.gridComplete=true;
            }
            maze.routeReady=false;
            maze.routeSignature.clear();
            maze.lastPlanOrigin={1e30f,1e30f,1e30f};
            maze.solvedNodes.clear();
            maze.solvedObstacles.clear();
        }else if(maze.meshCandidates.empty()){
            if(!detectedVariant.empty())maze.activeVariant=detectedVariant;
            maze.gridComplete=true;
        }

        // 4. Incrementally raycast the active MeshCollider (with automatic backend fallback & preset verification)
        if(!maze.gridComplete){
            Obj mc=maze.activeMeshCol.get();
            if(!mc||!alive(mc)||!solidCollider(mc)){
                if(!maze.routeReady){
                    espSummary="120041: chờ mở mê cung"+roundSuffix;
                    return;
                }
            }else{
                const int total=maze.meshW*maze.meshH;
                const int endIdx=std::min(total,maze.nextRayIdx+2500);
                for(int idx=maze.nextRayIdx;idx<endIdx;++idx){
                    float x=maze.meshX0+float(idx%maze.meshW)*maze.meshStep;
                    float z=maze.meshZ0+float(idx/maze.meshW)*maze.meshStep;
                    float hitY=std::numeric_limits<float>::quiet_NaN();
                    if(raycastColliderDown(mc,x,z,maze.meshBounds,floorTop,maze.raycastBackendMode,hitY)){
                        maze.meshHeights[std::size_t(idx)]=hitY;
                        if(hitY>floorTop+0.2f)++maze.wallHitCount;
                    }
                }
                maze.nextRayIdx=endIdx;
                // If after 300 rays on a large maze (>1000 cells) we have 0 wall hits (or >75% false wall hits), switch raycast backend mode (0 -> 1 -> 2)
                if(total>1000&&maze.nextRayIdx>=300&&(maze.wallHitCount==0||maze.wallHitCount>(maze.nextRayIdx*3)/4)&&maze.raycastBackendMode<2){
                    ++maze.raycastBackendMode;
                    maze.nextRayIdx=0;
                    maze.wallHitCount=0;
                    std::fill(maze.meshHeights.begin(),maze.meshHeights.end(),std::numeric_limits<float>::quiet_NaN());
                }
                if(maze.nextRayIdx>=total){
                    if(total>1000&&(maze.wallHitCount<100||maze.wallHitCount>(total*3)/4)&&!maze.activeVariant.empty()){
                        loadPresetGrid(maze.activeVariant);
                    }
                    maze.gridComplete=true;
                }
            }
        }

        // 5. Solve goal portal doorway and fastest corridor path from the player's current position (playerPos) to ĐÍCH
        MazePortalInfo portal=solveMazeGoalPortal(goalWalls,goalPos,floorB,bodyRadius,bodyHeight,gates);
        if(portal.ready&&maze.meshW>=2&&maze.meshH>=2){
            // Ensure the open goal doorway aperture (portal.approach -> portal.inside) is never blocked in meshHeights by overhead lintels
            for(int tStep=0;tStep<=4;++tStep){
                float frac=float(tStep)*0.25f;
                float px=portal.approach.x+frac*(portal.inside.x-portal.approach.x);
                float pz=portal.approach.z+frac*(portal.inside.z-portal.approach.z);
                int cx=int(std::round((px-maze.meshX0)/maze.meshStep));
                int cz=int(std::round((pz-maze.meshZ0)/maze.meshStep));
                for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
                    int xx=cx+dx,zz=cz+dz;
                    if(xx>=0&&xx<maze.meshW&&zz>=0&&zz<maze.meshH){
                        maze.meshHeights[std::size_t(zz*maze.meshW+xx)]=std::numeric_limits<float>::quiet_NaN();
                    }
                }
            }
        }

        const bool playerOnFloor=valid(playerPos)&&
            std::fabs(playerPos.x-floorB.center.x)<=floorB.extents.x&&
            std::fabs(playerPos.z-floorB.center.z)<=floorB.extents.z&&
            std::fabs(playerPos.y-floorTop)<=2.5f;
        Vec3 planOrigin=playerOnFloor?Vec3{playerPos.x,floorTop,playerPos.z}:Vec3{startPos.x,floorTop,startPos.z};

        auto segClear=[&](Vec3 a,Vec3 b)->bool{
            for(const auto&obs:maze.solvedObstacles){
                if(mazeSegmentHitsBox(a,b,obs))return false;
            }
            return true;
        };

        const bool playerMoved=(distXZ(planOrigin,maze.lastPlanOrigin)>=0.75f);
        const bool nextCornerBlocked=(maze.solvedNodes.size()>=2&&!segClear(planOrigin,maze.solvedNodes[1].anchor));
        std::string sig=maze.activeVariant+":"+std::to_string(allSolidBoxes.size())+":"+std::to_string(gates.size())+":"+std::to_string(maze.wallHitCount)+":"+(maze.gridComplete?"1":"0");

        if(!maze.routeReady||maze.routeSignature!=sig||playerMoved||nextCornerBlocked){
            Vec3 liveTarget=portal.ready?portal.goal:goalPos;
            Vec3 insideTarget=portal.ready?portal.inside:goalPos;

            auto trySolveFrom=[&](Vec3 orig)->MazeSolveResult{
                MazeSolveResult s=solveMazeGrid(
                    maze.meshStep,maze.meshW,maze.meshH,maze.meshX0,maze.meshZ0,
                    maze.meshHeights,allSolidBoxes,floorB,orig,liveTarget,bodyRadius,bodyHeight);
                if(!s.ready&&portal.ready){
                    std::set<std::size_t> finishBlockers(portal.blockerIndices.begin(),portal.blockerIndices.end());
                    std::vector<Bounds> apertureBoxes=staticBoxes;
                    for(std::size_t gi=0;gi<gates.size();++gi){
                        if(gates[gi].isFinishWall&&!finishBlockers.contains(gi)){
                            apertureBoxes.push_back(gates[gi].bounds);
                        }
                    }
                    s=solveMazeGrid(
                        maze.meshStep,maze.meshW,maze.meshH,maze.meshX0,maze.meshZ0,
                        maze.meshHeights,apertureBoxes,floorB,orig,insideTarget,bodyRadius,bodyHeight);
                }
                if(!s.ready){
                    s=solveMazeGrid(
                        maze.meshStep,maze.meshW,maze.meshH,maze.meshX0,maze.meshZ0,
                        maze.meshHeights,staticBoxes,floorB,orig,insideTarget,bodyRadius,bodyHeight);
                }
                return s;
            };

            MazeSolveResult sol=trySolveFrom(planOrigin);
            if(!sol.ready&&distXZ(planOrigin,startPos)>0.5f){
                sol=trySolveFrom(Vec3{startPos.x,floorTop,startPos.z});
            }
            if(!sol.ready&&!maze.activeVariant.empty()&&loadPresetGrid(maze.activeVariant)){
                sol=trySolveFrom(planOrigin);
                if(!sol.ready&&distXZ(planOrigin,startPos)>0.5f){
                    sol=trySolveFrom(Vec3{startPos.x,floorTop,startPos.z});
                }
            }

            if(sol.ready&&sol.nodes.size()>=2){
                Vec3 endCenter{goalPos.x,floorTop+0.04f,goalPos.z};
                if(distXZ(sol.nodes.back().anchor,endCenter)>0.2f){
                    sol.nodes.back().goal=false;
                    sol.nodes.push_back({endCenter,"@maze_goal",false,0,true});
                }
                maze.routeSignature=sig;
                maze.lastPlanOrigin=planOrigin;
                maze.solvedNodes=std::move(sol.nodes);
                maze.solvedObstacles=std::move(sol.obstacles);
                maze.routeReady=true;
            }
        }

        // 6. Emit live fastest-path corridor nodes from @feet (playerPos) to ĐÍCH
        if(maze.routeReady&&maze.solvedNodes.size()>=2){
            const float lineY=(valid(playerPos)?playerPos.y:floorTop)+0.08f;
            routeNodes.push_back({{playerOnFloor?playerPos.x:maze.solvedNodes[0].anchor.x,lineY,playerOnFloor?playerPos.z:maze.solvedNodes[0].anchor.z},"@feet",false,0,false});
            for(std::size_t i=1;i<maze.solvedNodes.size();++i){
                Vec3 pt=maze.solvedNodes[i].anchor;
                pt.y=lineY;
                routeNodes.push_back({pt,maze.solvedNodes[i].key,false,0,maze.solvedNodes[i].goal});
            }
            currentRouteStage=0;
            espSummary="120041 ("+(maze.activeVariant.empty()?"Mê Cung":maze.activeVariant)+"): ĐÍCH + Line dẫn đường nhanh nhất ("+std::to_string(routeNodes.size()-1)+" đoạn)"+roundSuffix;
        }else if(maze.boxCandidates.empty()&&maze.meshCandidates.empty()){
            currentRouteStage=0;
            routeNodes.push_back({{playerPos.x,playerPos.y+0.08f,playerPos.z},"@feet",false,0,false});
            routeNodes.push_back({{goalPos.x,goalPos.y+0.15f,goalPos.z},"@maze_goal",false,0,true});
            espSummary="120041 ("+(maze.activeVariant.empty()?"Mê Cung":maze.activeVariant)+"): ĐÍCH + Line hướng đích"+roundSuffix;
        }else{
            espSummary="120041 ("+(maze.activeVariant.empty()?"Mê Cung":maze.activeVariant)+"): đã hiện ĐÍCH • đang tính đường mê cung"+roundSuffix;
        }
    }

    // 6. Additional endpoint maps from agent.js rageEndpoint (lines 4929-4939):
    // - MiniGameKMGGreenRedLight: endPointGo at +0x138
    // - MiniGameKMGCrossyRoad: CrossyRoadStageManager at +0x1D8 -> goal Transform at +0x20
    // - MiniGameKMGRunAway: hideObjectManager at +0x140 -> EndPoint at +0xB8
    void collectEndpointMap(Obj gameObj,const std::string&mapLabel,int mapId,Vec3 playerPos){
        Vec3 goalPos{};
        bool hasGoal=false;
        if(mapLabel=="MiniGameKMGGreenRedLight"){
            Obj endGo=readObjFieldNamed(gameObj,{"endKeppRoot","endPointGo","_endPointGo","EndPointGo","endPoint"});
            if((!endGo||!alive(endGo))&&a.hasNativeLayout)safeReadAt(gameObj,0x138,endGo);
            if(endGo&&alive(endGo)){
                try{goalPos=worldPos(getTransform(endGo));hasGoal=valid(goalPos);}catch(...){}
            }
        }else if(mapLabel=="MiniGameKMGCrossyRoad"){
            Obj stageMgr=readObjFieldNamed(gameObj,{"StageManager","stageManager","_stageManager"});
            if((!stageMgr||!alive(stageMgr))&&a.hasNativeLayout)safeReadAt(gameObj,0x1D8,stageMgr);
            if(stageMgr&&alive(stageMgr)){
                Obj endTr=readObjFieldNamed(stageMgr,{"GoalStageRoot","goalStageRoot","endPoint","goal"});
                if((!endTr||!alive(endTr))&&a.hasNativeLayout)safeReadAt(stageMgr,0x20,endTr);
                if(endTr&&alive(endTr)){
                    try{goalPos=worldPos(getTransform(endTr));hasGoal=valid(goalPos);}catch(...){}
                }
            }
        }else if(mapLabel=="MiniGameKMGRunAway"){
            if(Obj manager=getHideObjectManager(gameObj)){
                Obj endGo=readObjFieldNamed(manager,{"EndPoint","endPoint","endPointGo"});
                if((!endGo||!alive(endGo))&&a.hasNativeLayout)safeReadAt(manager,0xB8,endGo);
                if(endGo&&alive(endGo)){
                    try{goalPos=worldPos(getTransform(endGo));hasGoal=valid(goalPos);}catch(...){}
                }
            }
        }
        if(hasGoal){
            worldMarkers.push_back({goalPos,"green","ĐÍCH","@goal",0,true,true,true});
            if(valid(playerPos)){
                currentRouteStage=0;
                routeNodes.push_back({{playerPos.x,playerPos.y+0.08f,playerPos.z},"@feet",false,0,false});
                routeNodes.push_back({{goalPos.x,goalPos.y+0.15f,goalPos.z},"@goal",false,0,true});
            }
            espSummary=mapLabel+" (#"+std::to_string(mapId)+"): ĐÍCH + Line hướng đích";
        }else{
            espSummary=mapLabel+" (#"+std::to_string(mapId)+"): chờ điểm ĐÍCH";
        }
    }

    // Multi-source active 3D Camera resolver:
    // 1. Camera.get_main()
    // 2. MiniGameKMGFogMaze (+0x1E8) / MiniGameKMGCrossyRoad (+0x138) -> headUpLandMark -> mainCamera (+0x90)
    // 3. SystemHelper.get_CinemachineBrain() -> get_OutputCamera() / m_OutputCamera (+0x50)
    // 4. SystemHelper.get_Map() -> m_CurrentMap -> cinemachineBrain (+0x40) -> m_OutputCamera (+0x50)
    Obj resolveActiveCamera(){
        if(Method mMain=tryMethod(unity("Camera"),"get_main",0)){
            try{
                Obj cam=invoke(mMain,nullptr);
                if(cam&&alive(cam))return cam;
            }catch(...){}
        }
        Obj mg=miniGameRoot.get();
        if(mg&&alive(mg)){
            Obj landMark=readObjFieldNamed(mg,{"headUpLandMark","_headUpLandMark"});
            if((!landMark||!alive(landMark))&&a.hasNativeLayout){
                std::string ml=classLabel(mg);
                if(ml=="MiniGameKMGFogMaze")safeReadAt(mg,0x1E8,landMark);
                else if(ml=="MiniGameKMGCrossyRoad")safeReadAt(mg,0x1E0,landMark);
                else if(ml=="MiniGameKMGTwistRoad")safeReadAt(mg,0x1F8,landMark);
            }
            if(landMark&&alive(landMark)){
                Obj cam=readObjFieldNamed(landMark,{"mainCamera","_mainCamera"});
                if((!cam||!alive(cam))&&a.hasNativeLayout)safeReadAt(landMark,0x90,cam);
                if(cam&&alive(cam))return cam;
            }
        }
        if(Klass sysHelper=tryGame("SystemHelper")){
            if(Method mBrain=tryMethod(sysHelper,"get_CinemachineBrain",0)){
                try{
                    Obj brain=invoke(mBrain,nullptr);
                    if(brain&&alive(brain)){
                        Obj cam=readObjFieldNamed(brain,{"m_OutputCamera","OutputCamera"});
                        if((!cam||!alive(cam))&&a.hasNativeLayout)safeReadAt(brain,0x50,cam);
                        if(cam&&alive(cam))return cam;
                        if(Method mOut=tryMethod(a.object_get_class(brain),"get_OutputCamera",0)){
                            cam=invoke(mOut,brain);
                            if(cam&&alive(cam))return cam;
                        }
                    }
                }catch(...){}
            }
            if(Method mGetMap=tryMethod(sysHelper,"get_Map",0)){
                try{
                    Obj mapSys=invoke(mGetMap,nullptr);
                    if(mapSys&&alive(mapSys)){
                        Obj curMap=readObjFieldNamed(mapSys,{"m_CurrentMap","_currentMap","currentMap"});
                        if(curMap&&alive(curMap)){
                            Obj brain=readObjFieldNamed(curMap,{"cinemachineBrain","_cinemachineBrain"});
                            if((!brain||!alive(brain))&&a.hasNativeLayout)safeReadAt(curMap,0x40,brain);
                            if(brain&&alive(brain)){
                                Obj cam=readObjFieldNamed(brain,{"m_OutputCamera","OutputCamera"});
                                if((!cam||!alive(cam))&&a.hasNativeLayout)safeReadAt(brain,0x50,cam);
                                if(cam&&alive(cam))return cam;
                            }
                        }
                    }
                }catch(...){}
            }
        }
        return nullptr;
    }

    void projectCurrentWorldToScreen(Vec3 playerPos){
        points.clear();routeSegments.clear();
        if(worldMarkers.empty()&&routeNodes.empty())return;
        Obj cam=activeCamRoot.get();
        if(!cam||!alive(cam)||(lastTick-camCheckTime)>=0.5){
            camCheckTime=lastTick;
            Obj resolved=resolveActiveCamera();
            if(resolved&&alive(resolved)){
                if(resolved!=cam)activeCamRoot.set(a,resolved);
                cam=resolved;
                try{
                    Rect vr=unbox<Rect>(call(cam,"get_rect"));
                    if(std::isfinite(vr.w)&&std::isfinite(vr.h)&&vr.w>0.05f&&vr.h>0.05f)cachedViewport=vr;
                }catch(...){}
            }
        }
        if(!cam||!alive(cam)){needsSettle=true;return;}
        const Rect viewport=cachedViewport;
        Method mW2V=tryMethod(a.object_get_class(cam),"WorldToViewportPoint",1);
        if(!mW2V)return;

        auto project=[&](Vec3 p)->ProjectedPt{
            void*v[]={&p};
            Obj ex=nullptr;
            Obj boxed=a.runtime_invoke(mW2V,cam,v,&ex);
            if(ex||!boxed)return {};
            void*unboxed=a.object_unbox(boxed);
            if(!unboxed)return {};
            Vec3 vp{};
            std::memcpy(&vp,unboxed,sizeof(vp));
            if(!valid(vp))return {};
            return {viewport.x+vp.x*viewport.w,1.f-(viewport.y+vp.y*viewport.h),vp.z,true};
        };

        int ringCount=0;
        for(const auto&m:worldMarkers){
            if(!valid(m.anchor)||m.key=="@feet")continue;
            auto p=project(m.anchor);
            if(!p.ok)continue;

            EspItem item{};
            if(p.depth>0.01f&&p.x>=0.f&&p.x<=1.f&&p.y>=0.f&&p.y<=1.f){
                item.x=p.x;item.y=p.y;item.headY=p.y;
            }else if(m.key=="@maze_goal"||m.key=="@goal"||m.label=="ĐÍCH"){
                // Clamp off-screen Goal marker to screen border so ĐÍCH is always visible!
                float vx=p.x-0.5f,vy=p.y-0.5f;
                if(p.depth<=0.01f){vx=-vx;vy=-vy;}
                float scale=std::max(std::fabs(vx),std::fabs(vy));
                if(scale<1e-4f){vx=0.f;vy=-0.44f;}
                else{vx=(vx/scale)*0.44f;vy=(vy/scale)*0.44f;}
                item.x=std::clamp(0.5f+vx,0.06f,0.94f);
                item.y=std::clamp(0.5f+vy,0.08f,0.92f);
                item.headY=item.y;
            }else{
                continue;
            }

            item.color=m.color;item.selected=m.selected;
            item.colorKind=(m.color=="red")?1:((m.color=="yellow")?2:0);
            item.showDistance=false; // distance is already formatted in item.label if m.showDistance is true
            item.distance=valid(playerPos)?std::round(dist3D(m.anchor,playerPos)*10.f)/10.f:0.f;
            if(m.showDistance&&std::isfinite(item.distance)&&item.distance>=0.f&&!m.label.empty()){
                char buf[96];
                if(item.distance<10.f)std::snprintf(buf,sizeof(buf),"%s • %.1fm",m.label.c_str(),double(item.distance));
                else std::snprintf(buf,sizeof(buf),"%s • %.0fm",m.label.c_str(),double(item.distance));
                item.label=buf;
            }else{
                item.label=m.label;
            }
            if(m.showRing&&ringCount<256&&p.depth>0.01f&&p.x>=0.f&&p.x<=1.f&&p.y>=0.f&&p.y<=1.f){
                constexpr float radius=0.8f;
                bool ringOk=true;
                for(int i=0;i<12;++i){
                    float angle=float(i)*(3.1415926535f/6.f);
                    Vec3 rp{m.anchor.x+radius*std::cos(angle),m.anchor.y+0.035f,m.anchor.z+radius*std::sin(angle)};
                    auto qp=project(rp);
                    if(!qp.ok||qp.depth<=0.01f||std::fabs(qp.x)>8.f||std::fabs(qp.y)>8.f){ringOk=false;break;}
                    item.ring[std::size_t(i)]={qp.x,qp.y};
                }
                if(ringOk){item.hasRing=true;++ringCount;}
            }
            points.push_back(std::move(item));
        }

        if(routeNodes.size()>=2){
            std::vector<ProjectedPt> pts(routeNodes.size());
            for(std::size_t i=0;i<routeNodes.size();++i)pts[i]=project(routeNodes[i].anchor);
            constexpr float nearZ=0.01f;
            for(std::size_t i=1;i<routeNodes.size();++i){
                ProjectedPt aPt=pts[i-1],bPt=pts[i];
                if(!aPt.ok||!bPt.ok)continue;
                if(aPt.depth<nearZ&&bPt.depth<nearZ)continue;
                if(aPt.depth<nearZ||bPt.depth<nearZ){
                    float t=(nearZ-aPt.depth)/(bPt.depth-aPt.depth);
                    Vec3 wa=routeNodes[i-1].anchor,wb=routeNodes[i].anchor;
                    Vec3 cutW{wa.x+t*(wb.x-wa.x),wa.y+t*(wb.y-wa.y),wa.z+t*(wb.z-wa.z)};
                    ProjectedPt cut=project(cutW);
                    if(!cut.ok||cut.depth<=0.f)continue;
                    if(aPt.depth<nearZ)aPt=cut;else bPt=cut;
                }
                float dx=bPt.x-aPt.x,dy=bPt.y-aPt.y;
                float lo=0.f,hi=1.f;bool ok=true;
                const std::pair<float,float> edges[4]={{-dx,aPt.x},{dx,1.f-aPt.x},{-dy,aPt.y},{dy,1.f-aPt.y}};
                for(auto[p,q]:edges){
                    if(std::fabs(p)<1e-12f){if(q<0.f)ok=false;continue;}
                    float t=q/p;
                    if(p<0.f)lo=std::max(lo,t);else hi=std::min(hi,t);
                }
                if(!ok||lo>hi)continue;
                auto clamp01=[](float v){return std::clamp(v,0.f,1.f);};
                RouteSegment seg{};
                seg.a={clamp01(aPt.x+lo*dx),clamp01(aPt.y+lo*dy)};
                seg.b={clamp01(aPt.x+hi*dx),clamp01(aPt.y+hi*dy)};
                seg.x0=seg.a.x;seg.y0=seg.a.y;
                seg.x1=seg.b.x;seg.y1=seg.b.y;
                seg.drop=routeNodes[i].drop;
                seg.dashed=seg.drop;
                seg.currentStage=(routeNodes[i].stage==currentRouteStage);
                seg.colorKind=seg.drop?1:(seg.currentStage?0:2);
                routeSegments.push_back(seg);
            }
        }
    }

    void espTick(Obj actorObj,double time){
        Vec3 playerPos{};
        try{playerPos=position(actorObj);}catch(...){return;}
        const double scanInterval=((currentMapId==120041||currentMapName=="MiniGameKMGFogMaze")&&!maze.gridComplete)?0.012:0.08;
        if(time-espScanTime>=scanInterval){
            espScanTime=time;
            worldMarkers.clear();routeNodes.clear();
            Obj gameObj=findMiniGame(actorObj);
            if(!gameObj||!alive(gameObj)){
                currentMapName.clear();currentMapId=0;
                points.clear();routeSegments.clear();
                espSummary="ESP/Line: Đang ở sảnh ("+(controllerName.empty()?"ActorDefaultControlPlayer":controllerName)+") • Tự hiện khi vào Minigame";
                return;
            }
            currentMapName=classLabel(gameObj);
            currentMapId=getMiniGameId(gameObj);
            int battleState=getBattleState(gameObj);

            if(currentMapName=="MiniGameKMGRealBoard"||(currentMapName=="MiniGameKMGTwistRoad"&&currentMapId==120018)){
                collectRealBoardOrGlass(gameObj,currentMapId==120018&&currentMapName=="MiniGameKMGTwistRoad");
            }else if(currentMapName=="MiniGameKMGTwistRoad"&&currentMapId==120026){
                collectRoad120026(gameObj,battleState);
            }else if(currentMapName=="MiniGameKMGTwistRoad"&&currentMapId==120006){
                collectDoors120006(gameObj,battleState,playerPos);
            }else if(currentMapName=="MiniGameKMGTwistRoad"&&currentMapId==120015){
                collectHoles120015(gameObj,battleState,playerPos);
            }else if(currentMapName=="MiniGameKMGFogMaze"||currentMapId==120041){
                collectMaze120041(actorObj,gameObj,battleState,playerPos);
            }else if(currentMapName=="MiniGameKMGDuel"||currentMapId==120019){
                std::uint8_t isAtt=0,canAtt=0,ended=0;std::uint32_t rnd=0;std::int32_t sc=0;
                readDuelState(gameObj,isAtt,canAtt,rnd,ended,sc);
                espSummary="Duel 120019: round "+std::to_string(rnd)+" • "+((isAtt==1&&canAtt==1)?"CÓ THỂ ĐÁNH":"chưa đến lượt")+" • điểm "+std::to_string(sc);
            }else if(currentMapName=="MiniGameKMGGreenRedLight"||currentMapName=="MiniGameKMGCrossyRoad"||currentMapName=="MiniGameKMGRunAway"){
                collectEndpointMap(gameObj,currentMapName,currentMapId,playerPos);
            }else{
                espSummary="Map hiện tại ("+currentMapName+" #"+std::to_string(currentMapId)+"): chờ map hỗ trợ";
            }
        }
        projectCurrentWorldToScreen(playerPos);
    }
};

GameBackend::GameBackend(Resolver r):state(std::make_unique<State>(std::move(r))){}
GameBackend::~GameBackend()=default;
const std::vector<EspItem>&GameBackend::esp()const{return state->points;}
const std::vector<RouteSegment>&GameBackend::route()const{return state->routeSegments;}
std::string GameBackend::diagnostic()const{
    auto&s=*state;
    return s.status+"\nController: "+(s.controllerName.empty()?"none":s.controllerName)+
        "\nMiniGame: "+(s.currentMapName.empty()?"none":s.currentMapName+" ("+std::to_string(s.currentMapId)+")")+
        "\nJump requests sent (not confirmed jumps): "+std::to_string(s.jumpRequests)+
        "\nESP markers: "+std::to_string(s.points.size())+" | Line segments: "+std::to_string(s.routeSegments.size())+
        "\n"+s.stealth.line()+
        "\nClient: "+(s.clientFlavor.empty()?"probe pending (ModuleSystem not reached)":s.clientFlavor)+
        "\nRuntime gate: "+(s.idle?"idle (no game calls)":"active");
}
void GameBackend::stop(Model&m){
    auto&s=*state;m.enabled.fill(false);s.points.clear();s.routeSegments.clear();
    s.clearCaches();s.resetFocus();s.espScanTime=-1;
    s.settleUntil=0;s.retryAfter=0;s.idle=true;
    m.status="Đã tắt ESP, Line, Spam nhảy, Tự sẵn sàng và Tự động búa";s.status=m.status;
}
void GameBackend::suspend(double time){
    auto&s=*state;s.points.clear();s.routeSegments.clear();
    s.clearCaches();s.resetFocus();s.espScanTime=-1;s.retryAfter=0;
    s.needsSettle=true;
    s.settleUntil=std::isfinite(time)?time+.75:0;
}
void GameBackend::toggle(Model&m,int id){
    if(id<0||id>=FeatureCount||!m.available[id]||!m.connected||!m.controlsAllowed)return;
    bool on=!m.enabled[id];m.enabled[id]=on;
    if(id==SpamJump){state->nextJump=0;state->lastJump=-1;if(!on)state->controlRoot.reset();}
    if(id==AutoReady){
        state->simpleLatched=false;state->partyLatched=false;state->readyScanTime=-1;
        state->simpleEligibleSince=-1;state->partyEligibleSince=-1;state->nextReadyClickTime=0;
        if(!on){state->simpleDialogRoot.reset();state->partyDialogRoot.reset();state->readySummary.clear();}
    }
    if(id==AutoHammer){
        state->hammerWindowKey.clear();state->hammerFightTime=-1;state->hammerLatched=false;
        if(!on)state->hammerSummary.clear();
    }
    if(id==Esp&&!on)state->points.clear();
    if(id==Line&&!on)state->routeSegments.clear();
    state->settleUntil=0;
    const char*label=id==Esp?"ESP":(id==Line?"Line":(id==SpamJump?"Spam nhảy":(id==AutoReady?"Tự sẵn sàng":"Tự động búa")));
    m.toast=std::string(label)+(on?" • BẬT":" • TẮT");m.toastUntil=m.time+1.;
}
void GameBackend::tick(Model&m,double time){
    auto&s=*state;
    try{
        if(!m.connected||!m.controlsAllowed){
            if(!s.idle)stop(m);
            else{m.status="Chờ kết nối game";s.status=m.status;}
            return;
        }
        if(!std::isfinite(time)||time<0){s.points.clear();s.routeSegments.clear();m.status="Thời gian cập nhật không hợp lệ";return;}
        if(time<s.lastTick){s.nextJump=0;s.lastJump=-1;s.espScanTime=-1;s.settleUntil=0;s.retryAfter=0;}
        s.lastTick=time;
        if(s.a.hasNativeLayout&&s.clientFlavor.empty()){
            s.probeClientFlavorReadOnly();
        }
        // Only run reactive sanitizer when SpamJump is actively enabled and held;
        // ESP / Line / AutoReady / AutoHammer run in 100% passive read-only mode
        // with zero memory or code modifications to keep the game account safe.
        if(m.enabled[SpamJump]){
            s.sanitizeLiveAntiCheat(time);
        }
        if(!runtimeRequested(m)){
            s.points.clear();s.routeSegments.clear();
            if(!s.idle){s.clearCaches();s.resetFocus();s.espScanTime=-1;s.idle=true;m.player.clear();}
            s.status="Chờ bật ESP, Line, Spam nhảy hoặc Tự sẵn sàng";m.status=s.status;
            return;
        }
        if(!m.license.valid){
            s.points.clear();s.routeSegments.clear();
            s.status="Chờ kích hoạt Key bản quyền";
            m.status=s.status;
            return;
        }
        s.idle=false;
        if(time<s.retryAfter){m.status=s.status;return;}
        auto actorObj=s.actor();
        if(!s.alive(actorObj)){
            s.points.clear();s.routeSegments.clear();s.clearCaches();s.resetFocus();m.player.clear();
            s.needsSettle=true;
            s.status="Chờ nhân vật local";m.status=s.status;return;
        }
        std::int64_t rawUid=0;
        if(Field fUid=s.tryField(s.a.object_get_class(actorObj),"<SUID>k__BackingField")){
            rawUid=s.read<std::int64_t>(actorObj,fUid,0x0a);
        }else if(s.a.hasNativeLayout){
            safeReadAt(actorObj,0x28,rawUid);
        }
        auto uid=std::to_string(rawUid);
        if(actorObj!=s.actorRoot.get()||uid!=s.actorUID){
            const bool hadActor=s.actorRoot.get()!=nullptr;
            s.clearCaches();s.resetFocus();s.actorRoot.set(s.a,actorObj);s.actorUID=uid;
            if(hadActor||s.needsSettle){s.settleUntil=time+.75;s.needsSettle=false;}
        }
        m.player=uid;
        if(time<s.settleUntil){s.points.clear();s.routeSegments.clear();s.status="Đang chờ cảnh ổn định";m.status=s.status;return;}
        std::string detail;
        auto control=s.currentControl(actorObj);(void)control;
        if(m.enabled[AutoHammer]){
            std::string hammerNote;
            try{
                s.hammerTick(actorObj,time,m.hammerDelayMs,hammerNote);
            }catch(const std::exception&e){
                hammerNote=std::string("Tự động búa: ")+e.what();
            }
            if(!hammerNote.empty()){
                if(detail.empty())detail=hammerNote;
                else if(hammerNote.find("Chờ vào map Búa")==std::string::npos&&hammerNote.find("Chờ map Búa")==std::string::npos)detail=hammerNote+" | "+detail;
            }
        }
        if(m.enabled[SpamJump]){
            std::string jumpNote;
            try{s.jumpTick(actorObj,time,m.jumpIntervalMs,jumpNote,m.jumpPressed,m.jumpButtonPos);}
            catch(const std::exception&e){s.nextJump=time+.25;jumpNote=std::string("Spam nhảy: ")+e.what();}
            if(!jumpNote.empty()){
                if(detail.empty()||detail.find("Chờ vào map Búa")!=std::string::npos||detail.find("Chờ map Búa")!=std::string::npos)detail=jumpNote;
                else if(jumpNote.find("ĐANG NHẢY")!=std::string::npos||jumpNote.find("đã về đích")!=std::string::npos||jumpNote.find("raised an exception")!=std::string::npos)detail+=" | "+jumpNote;
            }
        }else s.controlRoot.reset();
        if(m.enabled[AutoReady]){
            std::string readyNote;
            try{
                s.readyTick(control,time,readyNote);
                if(!readyNote.empty()){
                    if(detail.empty())detail=readyNote;
                    else if(readyNote.find("Đã bấm")!=std::string::npos||readyNote.find("Game báo")!=std::string::npos)detail+=" | "+readyNote;
                }
            }catch(const std::exception&e){
                if(!detail.empty())detail+=" | ";
                detail+=std::string("Tự sẵn sàng: ")+e.what();
            }
        }
        if(m.enabled[Esp]||m.enabled[Line]){
            try{
                s.espTick(actorObj,time);
                if(!m.enabled[Esp])s.points.clear();
                if(!m.enabled[Line])s.routeSegments.clear();
                if(!s.espSummary.empty()){
                    if(detail.empty()||
                       detail=="Tự sẵn sàng: BẬT • Chờ bảng Sẵn sàng"||
                       detail=="Tự sẵn sàng: Đang trong trận • Tự bật lại khi về sảnh/tiệc"||
                       detail=="Spam nhảy: BẬT • Đè nút Nhảy để spam"||
                       detail.find("Chờ vào map Búa")!=std::string::npos||
                       detail.find("Chờ map Búa")!=std::string::npos){
                        detail=s.espSummary;
                    }else{
                        detail=s.espSummary+" | "+detail;
                    }
                }
            }catch(const std::exception&e){
                s.points.clear();s.routeSegments.clear();s.needsSettle=true;
                if(!detail.empty())detail+=" | ";
                detail+=std::string("ESP/Line: ")+e.what();
            }
        }else{
            s.points.clear();s.routeSegments.clear();
        }
        s.retryAfter=0;
        m.status=detail.empty()?"ESP Minigame / Line / Tự nhảy / Tự sẵn sàng đang hoạt động":detail;s.status=m.status;
    }catch(const std::exception&e){
        s.points.clear();s.routeSegments.clear();s.clearCaches();s.resetFocus();m.player.clear();
        s.needsSettle=true;
        s.status=std::string("Chờ cảnh: ")+e.what();m.status=s.status;
        s.retryAfter=std::isfinite(time)?time+.75:0;
    }catch(...){
        s.points.clear();s.routeSegments.clear();s.clearCaches();s.resetFocus();m.player.clear();
        s.needsSettle=true;
        s.status="Backend lỗi không xác định";m.status=s.status;
        s.retryAfter=std::isfinite(time)?time+.75:0;
    }
}
}
