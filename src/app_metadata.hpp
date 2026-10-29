#pragma once
#include <string>
namespace vt::ios {
// Supported bundles: Play Together VNG (com.vng.playtogether, ptg.vng) and Play Together Global (com.haegin.playtogether)
inline constexpr const char* ExpectedBundles[]={"com.vng.playtogether","ptg.vng","com.haegin.playtogether"};
inline constexpr const char* ExpectedVersion="2.32.0";
inline constexpr const char* ExpectedBuild="227628";
inline bool bundleAccepted(const std::string& bundle){
    for(const char* accepted:ExpectedBundles)if(bundle==accepted)return true;
    return false;
}
inline std::string expectedBundlesText(){
    std::string text;
    for(const char* accepted:ExpectedBundles){if(!text.empty())text+=" or ";text+=accepted;}
    return text;
}
struct AppMetadata {
    bool present=false;
    std::string bundle,version,build;
};
inline bool matchesExpected(const AppMetadata& m){
    return m.present&&bundleAccepted(m.bundle)&&m.version==ExpectedVersion&&m.build==ExpectedBuild;
}
struct MetadataDecision {bool compatible=false,retryable=false;std::string source;};
inline MetadataDecision chooseMetadata(const AppMetadata& disk,const AppMetadata& bundle){
    // Prefer the actual installed plist over a cached dictionary. A readable
    // but different disk plist must never be overridden by a matching cache.
    if(disk.present)return {matchesExpected(disk),false,"installed Info.plist"};
    if(bundle.present)return {matchesExpected(bundle),false,"NSBundle fallback"};
    return {false,true,"metadata unavailable"};
}
inline std::string metadataLine(const char* source,const AppMetadata& m){
    auto value=[](const std::string& s){return s.empty()?"<missing>":s;};
    if(!m.present)return std::string(source)+": unavailable";
    return std::string(source)+": bundle="+value(m.bundle)+"; version="+value(m.version)+"; build="+value(m.build);
}
}
