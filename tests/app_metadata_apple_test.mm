#include "app_metadata_apple.hpp"
#include <cassert>
#include <cstdio>
int main(){@autoreleasepool{
    using namespace vt::ios;
    NSDictionary* strings=@{@"CFBundleIdentifier":@"com.vng.playtogether",@"CFBundleShortVersionString":@"2.32.0",@"CFBundleVersion":@"227628"};
    assert(matchesExpected(metadataFromDictionary(strings)));
    NSDictionary* numeric=@{@"CFBundleIdentifier":@"com.vng.playtogether",@"CFBundleShortVersionString":@"2.32.0",@"CFBundleVersion":@227628};
    assert(matchesExpected(metadataFromDictionary(numeric)));
    assert(!metadataFromDictionary(@[@"not a dictionary"]).present);
    assert(!metadataFromDictionary(nil).present);
    assert(metadataString(@" 227628\n")=="227628");
    assert(metadataString(NSNull.null).empty());
    NSString* directory=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSFileManager* files=NSFileManager.defaultManager;
    assert([files createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nullptr]);
    NSURL* url=[NSURL fileURLWithPath:[directory stringByAppendingPathComponent:@"Info.plist"]];
    for(NSPropertyListFormat format:{NSPropertyListXMLFormat_v1_0,NSPropertyListBinaryFormat_v1_0}){
        NSData* data=[NSPropertyListSerialization dataWithPropertyList:strings format:format options:0 error:nullptr];
        assert(data&&[data writeToURL:url atomically:YES]);
        assert(matchesExpected(metadataFromFile(url)));
    }
    assert([@"broken plist" writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:nullptr]);
    assert(!metadataFromFile(url).present);
    assert([files removeItemAtPath:directory error:nullptr]);
    assert(!metadataFromFile(url).present);
    const auto snapshot=readAppMetadata();
    assert(!metadataReport(snapshot).empty());
    std::puts("PASS: Foundation reader, XML/binary plist, NSNumber build, missing/invalid fields and executable-path lookup.");
}}
