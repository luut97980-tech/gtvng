#pragma once
#import <Foundation/Foundation.h>
#include "app_metadata.hpp"
#include <mach-o/dyld.h>
#include <vector>
namespace vt::ios {
inline std::string metadataString(id value){
    NSString* string=nil;
    if([value isKindOfClass:NSString.class])string=(NSString*)value;
    else if([value isKindOfClass:NSNumber.class])string=[(NSNumber*)value stringValue];
    string=[string stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    const char* utf8=string.UTF8String;
    return utf8?utf8:"";
}
inline AppMetadata metadataFromDictionary(id value){
    if(![value isKindOfClass:NSDictionary.class])return {};
    NSDictionary* dictionary=(NSDictionary*)value;
    return {true,metadataString(dictionary[@"CFBundleIdentifier"]),
        metadataString(dictionary[@"CFBundleShortVersionString"]),metadataString(dictionary[@"CFBundleVersion"])};
}
inline AppMetadata metadataFromFile(NSURL* url){
    if(!url)return {};
    NSData* data=[NSData dataWithContentsOfURL:url];
    if(!data)return {};
    id plist=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nullptr error:nullptr];
    return metadataFromDictionary(plist);
}
struct MetadataSnapshot {AppMetadata disk,bundle;};
inline MetadataSnapshot readAppMetadata(){
    MetadataSnapshot snapshot;
    NSBundle* bundle=NSBundle.mainBundle;
    snapshot.bundle=metadataFromDictionary(bundle.infoDictionary);
    // Resolve from the running executable, so the primary read does not depend
    // on NSBundle's cached version fields or localized InfoPlist.strings.
    uint32_t size=0;
    _NSGetExecutablePath(nullptr,&size);
    if(size>0&&size<=1024*1024){
        std::vector<char> path(size);
        if(_NSGetExecutablePath(path.data(),&size)==0){
            NSString* executable=[NSString stringWithUTF8String:path.data()];
            NSString* info=[executable.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"Info.plist"];
            if(info)snapshot.disk=metadataFromFile([NSURL fileURLWithPath:info]);
        }
    }
    if(!snapshot.disk.present)snapshot.disk=metadataFromFile([bundle.bundleURL URLByAppendingPathComponent:@"Info.plist"]);
    return snapshot;
}
inline std::string metadataReport(const MetadataSnapshot& s){
    return metadataLine("Installed plist",s.disk)+"\n"+metadataLine("NSBundle dictionary",s.bundle)+
        "\nExpected: bundle="+expectedBundlesText()+"; version="+ExpectedVersion+"; build="+ExpectedBuild;
}
}
