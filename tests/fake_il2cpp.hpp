#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace fake {
struct Field {std::string name;int type;std::size_t size;void*staticValue=nullptr;};
struct Class {std::uint32_t magic=0xC1A55001u;std::string name,ns;Class*parent=nullptr;std::map<std::string,Field> fields;};
struct Object {
    Class*klass;
    bool alive=true;
    std::map<Field*,std::array<unsigned char,16>> data;
    std::array<unsigned char,64> boxed{};
    std::string objName;
    Object*parentTr=nullptr;
    Object*ownerGo=nullptr;
    std::vector<Object*> childTrs;
    std::vector<Object*> components;
    float pos[3]{0.f,0.f,0.f};
    float bounds[6]{0.f,0.f,0.f,0.f,0.f,0.f};
    bool colEnabled=true;
    bool isTrigger=false;
    explicit Object(Class*c):klass(c){}
};
struct ManagedString {
    Class*klass=nullptr;
    void*monitor=nullptr;
    std::int32_t length=0;
    char16_t chars[128]{};
};
struct Method{void*methodPointer=nullptr;std::string name;};
inline std::map<std::string,Class> classes;
inline std::map<std::string,Method> methods;
inline std::map<std::uintptr_t,Object*> roots;
inline std::uintptr_t nextRoot=std::uintptr_t(1)<<33;
inline Object *system=nullptr,*actor=nullptr;
inline Object boolean{nullptr},box{nullptr};
inline ManagedString strBuf{};
inline Object*camera=nullptr;inline std::uint64_t jumps=0,readyClicks=0,hammerHits=0;inline Object*lastJumpTarget=nullptr;inline bool jumpException=false;
inline bool lastCanJumpDuringOnJump=false;
inline std::int8_t currentBattleState=5;
inline Class& cls(const char*n,const char*ns=""){auto&c=classes[n];c.name=n;c.ns=ns;return c;}
inline Field&field(Class&c,const char*n,int type,std::size_t size){auto&f=c.fields[n];f={n,type,size};return f;}
template<class T>void set(Object&o,Field&f,T value){assert(sizeof(T)==f.size);std::memcpy(o.data[&f].data(),&value,sizeof(value));}
template<class T>T get(Object&o,Field&f){T value{};std::memcpy(&value,o.data[&f].data(),sizeof(value));return value;}
inline void*domain_get(){return &classes;}
inline void**domain_get_assemblies(void*,std::size_t*n){static void*arr[]={reinterpret_cast<void*>(1),reinterpret_cast<void*>(2)};*n=2;return arr;}
inline void*assembly_get_image(void*p){return p;}
inline const char*image_get_name(void*p){return p==reinterpret_cast<void*>(1)?"Assembly-CSharp.dll":"UnityEngine.CoreModule.dll";}
inline void*class_from_name(void*,const char*ns,const char*n){auto i=classes.find(n);if(i!=classes.end()&&i->second.ns==(ns?ns:""))return &i->second;if(i!=classes.end())return nullptr;return &cls(n,ns);}
inline void*class_get_parent(void*c){assert(!c||static_cast<Class*>(c)->magic==0xC1A55001u);return static_cast<Class*>(c)->parent;}
inline const char*class_get_name(void*c){return static_cast<Class*>(c)->name.c_str();}
inline const char*class_get_namespace(void*c){return static_cast<Class*>(c)->ns.c_str();}
inline const void*class_get_method_from_name(void*,const char*n,int){auto&m=methods[n];m.name=n;return &m;}
inline void*class_get_field_from_name(void*c,const char*n){assert(c&&static_cast<Class*>(c)->magic==0xC1A55001u);auto&fields=static_cast<Class*>(c)->fields;auto i=fields.find(n);return i==fields.end()?nullptr:&i->second;}
inline const void*field_get_type(void*f){return f;}
inline int type_get_type(const void*f){return static_cast<const Field*>(f)->type;}
inline void field_get_value(void*o,void*f,void*out){auto&obj=*static_cast<Object*>(o);auto&field=*static_cast<Field*>(f);std::memcpy(out,obj.data[&field].data(),field.size);}
inline void field_set_value(void*o,void*f,void*val){auto&obj=*static_cast<Object*>(o);auto&field=*static_cast<Field*>(f);std::memcpy(obj.data[&field].data(),val,field.size);}
inline void field_static_get_value(void*f,void*out){auto*fld=static_cast<Field*>(f);void*v=(fld&&fld->staticValue)?fld->staticValue:system;std::memcpy(out,&v,sizeof(v));}
inline void*runtime_invoke(const void*m,void*object,void**args,void**ex){
    *ex=nullptr;const auto&name=static_cast<const Method*>(m)->name;
    bool result=false;
    if(name=="op_Implicit")result=args[0]&&static_cast<Object*>(args[0])->alive;
    else if(name=="get_MyActorCharacter")return actor;
    else if(name=="OnJump"){
        lastJumpTarget=static_cast<Object*>(object);
        if(lastJumpTarget&&lastJumpTarget->klass){
            if(void*f=class_get_field_from_name(lastJumpTarget->klass,"_canJump")){
                bool cj=false;
                field_get_value(lastJumpTarget,f,&cj);
                lastCanJumpDuringOnJump=cj;
            }
        }
        if(jumpException){*ex=&boolean;return nullptr;}
        ++jumps;return nullptr;
    }
    else if(name=="get_position"){
        box.boxed.fill(0);
        if(object)std::memcpy(box.boxed.data(),static_cast<Object*>(object)->pos,12);
        return &box;
    }
    else if(name=="get_bounds"){
        box.boxed.fill(0);
        if(object)std::memcpy(box.boxed.data(),static_cast<Object*>(object)->bounds,24);
        return &box;
    }
    else if(name=="get_name"){
        strBuf.klass=&cls("String","System");
        const std::string&s=object?static_cast<Object*>(object)->objName:std::string();
        strBuf.length=static_cast<std::int32_t>(std::min<std::size_t>(s.size(),127));
        for(std::int32_t i=0;i<strBuf.length;++i)strBuf.chars[i]=static_cast<unsigned char>(s[std::size_t(i)]);
        return &strBuf;
    }
    else if(name=="get_main")return camera;
    else if(name=="get_rect"){float rect[4]={0,0,1,1};std::memcpy(box.boxed.data(),rect,16);return &box;}
    else if(name=="WorldToViewportPoint"){
        float p[3]={0,0,0};
        if(args&&args[0])std::memcpy(p,args[0],12);
        float vp[3]={0.5f+p[0]*0.02f,0.5f+p[2]*0.02f,10.0f};
        box.boxed.fill(0);
        std::memcpy(box.boxed.data(),vp,12);
        return &box;
    }
    else if(name=="get_OtherActorCharacter")return nullptr;
    else if(name=="FindObjectOfType"||name=="get_UI"||name=="get_Dialog")return nullptr;
    else if(name=="get_IsGoalIn"||name=="get_IsDeathInGame"||name=="get_IsStun")result=false;
    else if(name=="get_CurrentBattleState"){std::memcpy(boolean.boxed.data(),&currentBattleState,1);return &boolean;}
    else if(name=="get_gameObject"){
        auto*o=static_cast<Object*>(object);
        return (o&&o->ownerGo)?o->ownerGo:object;
    }
    else if(name=="get_transform"){
        auto*o=static_cast<Object*>(object);
        return (o&&o->ownerGo)?o->ownerGo:object;
    }
    else if(name=="get_parent"){
        auto*o=static_cast<Object*>(object);
        return o?o->parentTr:nullptr;
    }
    else if(name=="get_root"){
        auto*o=static_cast<Object*>(object);
        while(o&&o->parentTr)o=o->parentTr;
        return o;
    }
    else if(name=="get_childCount"){
        auto*o=static_cast<Object*>(object);
        int c=o?static_cast<int>(o->childTrs.size()):0;
        std::memcpy(boolean.boxed.data(),&c,sizeof(c));
        return &boolean;
    }
    else if(name=="GetChild"){
        auto*o=static_cast<Object*>(object);
        int idx=args&&args[0]?*static_cast<int*>(args[0]):0;
        return (o&&idx>=0&&std::size_t(idx)<o->childTrs.size())?o->childTrs[std::size_t(idx)]:nullptr;
    }
    else if(name=="get_activeInHierarchy"||name=="get_activeSelf"){result=object&&static_cast<Object*>(object)->alive;}
    else if(name=="get_enabled"){result=object&&static_cast<Object*>(object)->colEnabled;}
    else if(name=="get_isTrigger"){result=object&&static_cast<Object*>(object)->isTrigger;}
    else if(name=="get_layer"){int l=0;std::memcpy(boolean.boxed.data(),&l,sizeof(l));return &boolean;}
    else if(name=="GetComponentCount"){
        auto*o=static_cast<Object*>(object);
        int c=o?static_cast<int>(o->components.size()):0;
        std::memcpy(boolean.boxed.data(),&c,sizeof(c));
        return &boolean;
    }
    else if(name=="QueryComponentAtIndex"){
        auto*o=static_cast<Object*>(object);
        int idx=args&&args[0]?*static_cast<int*>(args[0]):0;
        return (o&&idx>=0&&std::size_t(idx)<o->components.size())?o->components[std::size_t(idx)]:nullptr;
    }
    else if(name=="Raycast_Injected"||name=="Raycast"){
        // Interior maze wall at z in [-3.6, -2.4], x in [-8.0, 2.5] blocking straight line (-6,0,-6) -> (0,0,0)
        float ray[6]{};
        if(args&&args[0])std::memcpy(ray,args[0],24);
        float rx=ray[0],rz=ray[2];
        bool hitWall=(rz>=-3.6f&&rz<=-2.4f&&rx>=-8.0f&&rx<=2.5f);
        if(name=="Raycast_Injected"){
            if(args&&args[2])*static_cast<bool*>(args[2])=hitWall;
            if(hitWall&&args&&args[3]){
                float pt[3]={rx,2.0f,rz};
                std::memcpy(args[3],pt,12);
            }
            return nullptr;
        }
        if(hitWall&&args&&args[1]){
            float pt[3]={rx,2.0f,rz};
            std::memcpy(args[1],pt,12);
        }
        boolean.klass=&cls("Boolean","System");
        result=hitWall;
    }
    else if(name=="OnClickReady"){++readyClicks;return nullptr;}
    else if(name=="OnAction"){++hammerHits;return nullptr;}
    else{assert(false&&"Unexpected managed method");}
    std::memcpy(boolean.boxed.data(),&result,sizeof(result));return &boolean;
}
inline void*object_get_class(void*o){return static_cast<Object*>(o)->klass;}
inline void*object_unbox(void*o){return static_cast<Object*>(o)->boxed.data();}
inline std::uintptr_t gchandle_new(void*o,bool){auto id=nextRoot++;roots[id]=static_cast<Object*>(o);return id;}
inline void*gchandle_get_target(std::uintptr_t id){return roots.at(id);}
inline void gchandle_free(std::uintptr_t id){assert(roots.erase(id)==1);}
inline void*array_new(void*,std::size_t){assert(false);return nullptr;}
inline std::size_t array_length(void*){assert(false);return 0;}
inline std::uint32_t array_object_header_size(){return 32;}
inline void*thread_current(){return &classes;}
inline void*thread_attach(void*){return &classes;}
inline void thread_detach(void*){assert(false);}
inline void*resolve(const char*n){
#define FN(x) if(std::strcmp(n,"il2cpp_" #x)==0)return reinterpret_cast<void*>(&x)
    FN(domain_get);FN(domain_get_assemblies);FN(assembly_get_image);FN(image_get_name);FN(class_from_name);
    FN(class_get_parent);FN(class_get_name);FN(class_get_namespace);FN(class_get_method_from_name);FN(class_get_field_from_name);
    FN(field_get_type);FN(type_get_type);FN(field_get_value);FN(field_set_value);FN(field_static_get_value);FN(runtime_invoke);
    FN(object_get_class);FN(object_unbox);FN(gchandle_new);FN(gchandle_get_target);FN(gchandle_free);
    FN(array_new);FN(array_length);FN(array_object_header_size);FN(thread_current);FN(thread_attach);FN(thread_detach);
#undef FN
    return nullptr;
}
}
