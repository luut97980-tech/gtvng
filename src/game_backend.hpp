#pragma once
#include "mobile_menu.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace vt::ios {
struct EspVec2 {float x=0,y=0;};
struct EspItem {
    float x=0,y=0,headY=0,distance=0;
    std::string label;
    std::string color="green";
    int colorKind=0;
    bool selected=false,hasRing=false,showDistance=false;
    std::array<EspVec2,12> ring{};
};
struct RouteSegment {
    EspVec2 a{},b{};
    float x0=0,y0=0,x1=0,y1=0;
    int colorKind=0;
    bool drop=false,dashed=false;
    bool currentStage=true;
};
// True only while a feature that needs the game runtime is switched on.
// The renderer uses it to keep the backend completely silent at the login
// screen and while the menu is just sitting there.
bool runtimeRequested(const Model&);
class GameBackend {
    struct State;
    std::unique_ptr<State> state;
public:
    using Resolver=std::function<void*(const char*)>;
    explicit GameBackend(Resolver resolver);
    ~GameBackend();
    void tick(Model&,double time);
    // Drops every managed handle and holds the next runtime call for a moment.
    // Called from layout/orientation/app-lifecycle edges, where the game is
    // rebuilding its scene and its objects are not safe to touch.
    void suspend(double time);
    void toggle(Model&,int feature);
    void stop(Model&);
    std::string diagnostic() const;
    const std::vector<EspItem>& esp() const;
    const std::vector<RouteSegment>& route() const;
};
}
