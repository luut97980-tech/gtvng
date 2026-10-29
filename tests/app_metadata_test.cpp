#include "app_metadata.hpp"
#include <cassert>
#include <cstdio>
int main(){
    using namespace vt::ios;
    AppMetadata correct{true,"com.vng.playtogether","2.32.0","227628"};
    AppMetadata altBundle{true,"ptg.vng","2.32.0","227628"};
    AppMetadata globalBundle{true,"com.haegin.playtogether","2.32.0","227628"};
    AppMetadata wrongBundle=correct;wrongBundle.bundle="com.example.resigned";
    AppMetadata wrongVersion=correct;wrongVersion.version="2.33.0";
    AppMetadata wrongBuild=correct;wrongBuild.build="227629";
    AppMetadata missingField=correct;missingField.build.clear();
    assert(matchesExpected(correct));
    assert(matchesExpected(altBundle));
    assert(matchesExpected(globalBundle));
    assert(chooseMetadata(altBundle,correct).compatible);
    assert(chooseMetadata(globalBundle,correct).compatible);
    assert(expectedBundlesText().find("ptg.vng")!=std::string::npos);
    assert(expectedBundlesText().find("com.haegin.playtogether")!=std::string::npos);
    for(const auto& bad:{wrongBundle,wrongVersion,wrongBuild,missingField}){
        assert(!matchesExpected(bad));
        assert(!chooseMetadata(bad,correct).compatible); // disk mismatch is authoritative
    }
    auto d=chooseMetadata(correct,wrongBundle);
    assert(d.compatible&&!d.retryable&&d.source=="installed Info.plist");
    d=chooseMetadata({},correct);assert(d.compatible&&d.source=="NSBundle fallback");
    d=chooseMetadata({},{});assert(!d.compatible&&d.retryable);
    d=chooseMetadata({},wrongVersion);assert(!d.compatible&&!d.retryable);
    const auto report=metadataLine("Installed plist",wrongBuild);
    assert(report.find("227629")!=std::string::npos&&report.find("com.vng.playtogether")!=std::string::npos);
    assert(metadataLine("runtime",missingField).find("<missing>")!=std::string::npos);
    std::puts("PASS: matching installed metadata, cache divergence, fallback, rejected mismatches and explicit observed values.");
}
