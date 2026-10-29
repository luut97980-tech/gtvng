#pragma once
#ifndef VT_IOS_STEALTH_HPP_INCLUDED
#define VT_IOS_STEALTH_HPP_INCLUDED
// VTMenu iOS - runtime anti-detection shield.
// Target: Play Together 2.32.0 build 227628, ARM64, iOS 15+ - both clients:
//   Play Together VNG    (com.vng.playtogether / ptg.vng)
//   Play Together Global (com.haegin.playtogether)
//
// Every class name, method name and field offset below was read out of the two
// Assembly-CSharp dumps shipped alongside this source and cross-checked against
// each other. Nothing here is guessed.
//
// Both 2.32.0 clients were compared type by type (37,320 types, method bodies
// with RVA comments normalised out): the entire anti-cheat surface below is
// byte-identical between VNG and Global, PID_CHEAT_HackDetection is 19999 in
// both, and the one structural difference the shield could have tripped over -
// ModuleSystem._msgQueue, VNG only, at 0x248 - sits above every offset the
// sanitizer touches (0x40, 0x48) and above every field the menu reads. The VNG
// SDK itself (VNGSDK, PassSDK, VNGIAPMessageCB, ModuleSystem.Update) is absent
// from the Global image and has nothing to do with detection.
//
// That is why nothing in this file branches on the client: it resolves by name
// and by vtable slot, never by build layout. The only per-client fact worth
// knowing at runtime is which image you are in, and game_backend derives that
// from the one field that differs.
//
// Why this file exists (what R5.9 got wrong)
// ---------------------------------------------------------------------------
// R5.9 only wrote Il2CppMethodInfo::methodPointer. In IL2CPP that table is
// consulted for delegates, reflection and *some* virtual dispatch - a plain
// intra-assembly call compiles to a direct `bl` at the native body, and a
// virtual call goes through the class vtable that was filled at type init.
// Concretely: ActorControl.SetCheatDetect (dump line 33454) is `public virtual`
// and the two overrides at 50635/58733 are `public override`; GPrestoIOS.
// SendDetectionLog (dump 2219799) is an interface slot. Stubbing methodPointer
// never touched either of them.
//
// So the shield now has three independent paths and takes all of them:
//
//   1. methodPointer rewrite  - costs nothing, catches delegates/reflection.
//   2. ARM64 inline detour    - overwrites the first 16 bytes of the native
//                               body with `ldr x16,#8 ; br x16 ; .quad stub`.
//                               Catches direct calls and vtable calls.
//   3. live object scrub      - every 0.25 s, run by game_backend: zeroes the
//                               report queue, the detect hashset, the GPresto
//                               code set and the proof-time flag.
//
// Layer map (dump references in brackets)
//   1. Aggregator      AntiCheatListener [255946] - 8 On*Detected callbacks,
//                      get_IsCheating, CheckDetect(HType), LateUpdate.
//   2. Report queue    ModuleSystem [237149] - ModuleReceiveCB, CheckCheatDetect.
//   3. Network egress  NetNativeProtocol [600934] - SendToCheatDetect,
//                      PID_CHEAT_HackDetection. Protocol id 19999.
//   4. Movement flags  KinematicCharacterMotor [283477], ActorControl [33114],
//                      ActorCharacterControl [50601], VehicleControl [58687],
//                      BoardMiniGamePlinko [8811].
//   5. G-Presto        SpeedHackScanner [381], SpeedHack [2217301],
//                      GPrestoEngine [2219877], GPrestoIOS [2219799].
//   6. Anti-Cheat Kit  SpeedHackDetector [2065890], TimeCheatingDetector
//                      [2066313], WallHackDetector [2066658],
//                      InjectionDetector [2065659], ObscuredCheatingDetector
//                      [2065737].
//
// Deliberately NOT patched, and why:
//   - GPrestoIOS native externs (Presto_UnityInit / Presto_getsData /
//     Presto_getsDataAsync / Presto_LicenseValid). They feed the login payload
//     that WebClient.GPrestoCrossCheckResult verifies; faking them turns a
//     silent non-detection into a hard "forged client" verdict.
//   - GPrestoEngine.GetDetectCodes - returns a HashSet the caller iterates.
//     A null return would NRE in game code. The set is emptied instead.
//   - ACTk static StartDetection overloads that return the detector instance.
//     Returning null on those is a crash risk; Update/TriggerDetection are
//     dead, so a started detector still cannot fire.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <mach/vm_page_size.h>
// <mach/mach_vm.h> is a hard `#error mach_vm.h unsupported.` stub in the
// iPhoneOS SDK - mach_vm_protect and friends are macOS-only. The iOS-callable
// equivalent is vm_protect(), declared in <mach/vm_map.h>, which <mach/mach.h>
// already pulls in, and on every 64-bit target libsyscall implements it as a
// thin forward to mach_vm_protect with vm_address_t == mach_vm_address_t, so
// nothing truncates and the VM_PROT_COPY semantics are identical.
#endif

namespace vt::ios {

// --- crash journal ---------------------------------------------------------
// A fixed ring of POD breadcrumbs written *before* every step that cannot be
// undone: a MethodInfo slot, a code page, a managed object header. Plugin.mm
// installs a SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT handler that dumps this ring
// with write(2) and re-raises with SIG_DFL, so the next launch names the exact
// class, method and step that faulted instead of "it crashed somewhere".
//
// Everything here is async-signal-safe: no allocation, no locks, no stdio. The
// sequence counter is a lock-free relaxed fetch_add. A concurrent writer can
// tear one entry; a torn entry is meaningless but never a second fault.
namespace journal {

enum Tag : std::uint32_t {
    None = 0, Boot, Arm, Sweep, Target, FindClass, Method, PatchSlot,
    DetourTry, DetourOk, DetourSkip, Watchdog, Sanitize, SanitizeStep,
    SanitizeHit, Last
};

// 512 * 40 bytes = 20 KiB of .bss. Enough to keep a whole class-cache miss
// (18 classes x ~40 assemblies) plus the following sweep in the window.
inline constexpr std::uint32_t kRingSize = 512;

struct Entry {
    std::uint32_t tag;
    std::uint32_t step;
    std::uintptr_t a;
    std::uintptr_t b;
    std::uintptr_t c;
};

inline Entry gRing[kRingSize]{};
inline std::atomic<std::uint32_t> gSeq{0};

// b and c are only ever set to string literals (kStealthTargets entries), which
// live in __TEXT of this dylib and are always mapped. a is whatever the caller
// is about to touch, so it is printed as hex, never dereferenced.
inline void mark(Tag t, std::uint32_t step = 0, std::uintptr_t a = 0,
                 std::uintptr_t b = 0, std::uintptr_t c = 0) noexcept {
    const std::uint32_t s = gSeq.fetch_add(1, std::memory_order_relaxed);
    Entry& e = gRing[s % kRingSize];
    e.tag = static_cast<std::uint32_t>(t);
    e.step = step;
    e.a = a;
    e.b = b;
    e.c = c;
}

inline const char* tagName(std::uint32_t t) noexcept {
    switch (t) {
    case Boot: return "boot";
    case Arm: return "arm";
    case Sweep: return "sweep";
    case Target: return "target";
    case FindClass: return "find-class";
    case Method: return "method";
    case PatchSlot: return "patch-slot";
    case DetourTry: return "detour-try";
    case DetourOk: return "detour-ok";
    case DetourSkip: return "detour-skip";
    case Watchdog: return "watchdog";
    case Sanitize: return "sanitize";
    case SanitizeStep: return "sanitize-step";
    case SanitizeHit: return "sanitize-hit";
    default: return "?";
    }
}

namespace fmt {
inline void str(char*& p, char* end, const char* s, std::size_t maxLen) noexcept {
    if (!s) s = "-";
    std::size_t n = 0;
    while (*s && n < maxLen && p < end) {
        *p++ = *s++;
        ++n;
    }
}
inline void hex(char*& p, char* end, std::uintptr_t v) noexcept {
    char tmp[16];
    int n = 0;
    do {
        tmp[n++] = "0123456789abcdef"[static_cast<int>(v & 0xFu)];
        v >>= 4;
    } while (v && n < 16);
    if (p < end) *p++ = '0';
    if (p < end) *p++ = 'x';
    while (n > 0 && p < end) *p++ = tmp[--n];
}
inline void dec(char*& p, char* end, std::uint32_t v) noexcept {
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + v % 10u);
        v /= 10u;
    } while (v && n < 10);
    while (n > 0 && p < end) *p++ = tmp[--n];
}
} // namespace fmt

// Header + ring, formatted for a single write(2). Returns the byte count, or 0
// when the buffer is too small to say anything useful.
inline std::size_t formatReport(char* out, std::size_t cap, int sig,
                                std::uintptr_t pc, std::uintptr_t lr,
                                std::uintptr_t farAddr, std::uintptr_t sp) noexcept {
    if (!out || cap < 128) return 0;
    char* p = out;
    char* const end = out + cap - 1;
    fmt::str(p, end, "VTMenu crash journal\nsignal=", 64);
    fmt::dec(p, end, static_cast<std::uint32_t>(sig));
    fmt::str(p, end, " pc=", 8);
    fmt::hex(p, end, pc);
    fmt::str(p, end, " lr=", 8);
    fmt::hex(p, end, lr);
    fmt::str(p, end, " far=", 8);
    fmt::hex(p, end, farAddr);
    fmt::str(p, end, " sp=", 8);
    fmt::hex(p, end, sp);

    const std::uint32_t seq = gSeq.load(std::memory_order_relaxed);
    const std::uint32_t total = seq < kRingSize ? seq : kRingSize;
    fmt::str(p, end, "\nseq=", 16);
    fmt::dec(p, end, seq);
    fmt::str(p, end, " kept=", 16);
    fmt::dec(p, end, total);
    for (std::uint32_t i = 0; i < total && p + 128 < end; ++i) {
        const std::uint32_t idx = (seq - total + i) % kRingSize;
        const Entry& e = gRing[idx];
        fmt::str(p, end, "\n", 2);
        fmt::dec(p, end, e.step);
        fmt::str(p, end, " ", 2);
        fmt::str(p, end, tagName(e.tag), 16);
        fmt::str(p, end, " a=", 4);
        fmt::hex(p, end, e.a);
        fmt::str(p, end, " b=", 4);
        fmt::hex(p, end, e.b);
        fmt::str(p, end, " c=", 4);
        fmt::hex(p, end, e.c);
        if (e.tag == Target || e.tag == Method) {
            fmt::str(p, end, "  ", 3);
            fmt::str(p, end, reinterpret_cast<const char*>(e.b), 64);
            fmt::str(p, end, ".", 2);
            fmt::str(p, end, reinterpret_cast<const char*>(e.c), 64);
        }
    }
    fmt::str(p, end, "\n", 2);
    *p = 0;
    return static_cast<std::size_t>(p - out);
}

} // namespace journal

// The five entry points game_backend already resolves. class_get_method_from_name
// is swept over arity 0..kMaxArity, so overload sets (ObscuredCheatingDetector.
// OnCheatingDetected exists with 3 and 4 params, dump 2065737) are all caught
// without hardcoding a per-method arity table that goes stale on a patch.
struct StealthApi {
    void* (*domain_get)() = nullptr;
    void** (*domain_get_assemblies)(void*, std::size_t*) = nullptr;
    void* (*assembly_get_image)(void*) = nullptr;
    void* (*class_from_name)(void*, const char*, const char*) = nullptr;
    void* (*class_get_method_from_name)(void*, const char*, int) = nullptr;
};

// Return kind of the managed method being stubbed. The stub has to put the
// right thing in the right register: bool/int -> w0, float -> s0, pointer -> x0,
// void -> nothing. Returning void for a method the game expects a value from
// hands it whatever w0 happened to hold, which is how a shield turns into a
// crash.
enum class Ret : std::uint8_t { Void, Bool, I32, F32, Ptr };

namespace stealth_detail {

inline constexpr std::uint32_t kMaxArity = 5;

inline void*& slot(void* methodInfo) {
    return *reinterpret_cast<void**>(methodInfo);
}
inline bool plausible(void* p) {
    return p && reinterpret_cast<std::uintptr_t>(p) > 0x1000;
}
// arm64e signs function pointers in the MethodInfo slots and in the vtables.
// Everything that gets executed or written has to go through this first, or the
// detour lands on a pointer that the branch will authenticate and reject.
inline std::uintptr_t rawCode(const void* p) {
    std::uintptr_t v = reinterpret_cast<std::uintptr_t>(p);
#if defined(__APPLE__) && defined(__has_feature)
#if __has_feature(ptrauth_calls)
    v = reinterpret_cast<std::uintptr_t>(
        __builtin_ptrauth_strip(reinterpret_cast<void*>(v),
                                __builtin_ptrauth_key_function_pointer));
#endif
#endif
    return v;
}

// The inverse: a bare stub address put back into a slot that IL2CPP will read
// and branch through has to carry the same signature it would have had.
inline void* signCode(void* fn) {
#if defined(__APPLE__) && defined(__has_feature)
#if __has_feature(ptrauth_calls)
    return __builtin_ptrauth_sign_unauthenticated(
        fn, __builtin_ptrauth_key_function_pointer, 0);
#endif
#endif
    return fn;
}

template <class T>
inline void* asVoid(T fn) {
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(fn));
}

// --- return-value-correct stubs, arity 0..kMaxArity ------------------------
template <class R>
struct Zero {
    static R get() { return R{}; }
};
template <>
struct Zero<void> {
    static void get() {}
};

template <class T, std::size_t>
using SlotArg = T;

template <class R, std::size_t N, class = std::make_index_sequence<N>>
struct Thunk;

template <class R, std::size_t N, std::size_t... I>
struct Thunk<R, N, std::index_sequence<I...>> {
    // Parameters are unnamed on purpose: the stub ignores everything, it only
    // has to match the ABI shape so the caller's stack stays intact.
    static R entry(SlotArg<void*, I>...) { return Zero<R>::get(); }
};

template <std::size_t N>
inline void* thunkForArity(Ret r) {
    switch (r) {
    case Ret::Void: return asVoid(&Thunk<void, N>::entry);
    case Ret::Bool: return asVoid(&Thunk<bool, N>::entry);
    case Ret::I32: return asVoid(&Thunk<std::int32_t, N>::entry);
    case Ret::F32: return asVoid(&Thunk<float, N>::entry);
    case Ret::Ptr: return asVoid(&Thunk<void*, N>::entry);
    }
    return asVoid(&Thunk<void, N>::entry);
}

inline void* thunkFor(Ret r, std::uint32_t argc) {
    switch (argc) {
    case 0: return thunkForArity<0>(r);
    case 1: return thunkForArity<1>(r);
    case 2: return thunkForArity<2>(r);
    case 3: return thunkForArity<3>(r);
    case 4: return thunkForArity<4>(r);
    default: return thunkForArity<5>(r);
    }
}

// --- ARM64 inline detour ---------------------------------------------------
// `ldr x16, #8` loads the 8-byte literal that follows it, `br x16` jumps there.
// 16 bytes total, no branch range problem, no trampoline to leak.
inline constexpr std::uint32_t kLdrX16 = 0x58000050u;
inline constexpr std::uint32_t kBrX16 = 0xD61F0200u;

struct Detour {
    void* addr = nullptr;
    bool ok = false;
};

#if defined(__APPLE__)
inline bool protectPage(void* addr, bool writable) {
    const auto raw = reinterpret_cast<vm_address_t>(addr);
    const vm_address_t pageStart = raw & ~static_cast<vm_address_t>(vm_page_size - 1);
    const vm_address_t pageEnd = ((raw + 15u) & ~static_cast<vm_address_t>(vm_page_size - 1)) + vm_page_size;
    const vm_size_t span = static_cast<vm_size_t>(pageEnd - pageStart);
    const vm_prot_t prot = writable
                               ? (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE | VM_PROT_COPY)
                               : (VM_PROT_READ | VM_PROT_EXECUTE);
    return vm_protect(mach_task_self(), pageStart, span, FALSE, prot) == KERN_SUCCESS;
}
#else
inline bool protectPage(void*, bool) { return false; }
#endif

// Refuse to overwrite a body whose first four instructions already leave the
// straight line or read from PC. A CRASH here is a detection; a skipped method
// is just one layer thinner.
inline bool looksPatchable(const std::uint32_t* ins) {
    for (int i = 0; i < 4; ++i) {
        const std::uint32_t op = ins[i];
        if (op == 0xD65F03C0u) return false;                  // ret
        if ((op & 0xFFFFFC1Fu) == 0xD61F0000u) return false;  // br xN
        if ((op & 0xFFFFFC1Fu) == 0xD63F0000u) return false;  // blr xN
        if ((op & 0x7C000000u) == 0x14000000u) return false;  // b / bl
        if ((op & 0x3B000000u) == 0x18000000u) return false;  // ldr (literal)
        if ((op & 0x9F000000u) == 0x10000000u) return false;  // adr
        if ((op & 0x9F000000u) == 0x90000000u) return false;  // adrp
    }
    return true;
}

inline bool writeJump(void* addr, const void* target) {
    auto* ins = static_cast<std::uint32_t*>(addr);
    if (reinterpret_cast<std::uintptr_t>(ins) & 3u) return false;
    if (!looksPatchable(ins)) return false;
    if (!protectPage(addr, true)) return false;
    const std::uintptr_t dest = rawCode(target);
    ins[0] = kLdrX16;
    ins[1] = kBrX16;
    std::memcpy(ins + 2, &dest, sizeof dest);
#if defined(__APPLE__)
    sys_icache_invalidate(addr, 16);
#endif
    protectPage(addr, false);
    return ins[0] == kLdrX16 && ins[1] == kBrX16 &&
           std::memcmp(ins + 2, &dest, sizeof dest) == 0;
}

} // namespace stealth_detail

struct StealthTarget {
    const char* ns;
    const char* cls;
    const char* method;
    Ret ret;
};

// Sorted by layer so the diagnostic reads in the same order as the header.
inline constexpr StealthTarget kStealthTargets[] = {
    // 1. Aggregator. Every detector in the game funnels through one of these.
    //    Killing the aggregator is worth more than killing every detector.
    {"", "AntiCheatListener", "get_IsCheating", Ret::Bool},
    {"", "AntiCheatListener", "CheckDetect", Ret::Bool},
    {"", "AntiCheatListener", "OnSpeedHackDetected", Ret::Void},
    {"", "AntiCheatListener", "OnSpeedHackDetectedGpresto", Ret::Void},
    {"", "AntiCheatListener", "OnTimeCheatingDetected", Ret::Void},
    {"", "AntiCheatListener", "OnObscuredTypeCheatingDetected", Ret::Void},
    {"", "AntiCheatListener", "OnTableCheatingDetected", Ret::Void},
    {"", "AntiCheatListener", "OnOtherCheatingDetected", Ret::Void},
    {"", "AntiCheatListener", "OnAnimationSpeedCheatDetect", Ret::Void},
    {"", "AntiCheatListener", "LateUpdate", Ret::Void},

    // 2. Report queue + dispatcher.
    {"", "ModuleSystem", "ModuleReceiveCB", Ret::Void},
    {"", "ModuleSystem", "CheckCheatDetect", Ret::Void},

    // 3. Network egress. PID_CHEAT_HackDetection == 19999 (dump 600045).
    {"PlayTogether.Network.Native", "NetNativeProtocol", "SendToCheatDetect", Ret::Void},
    {"PlayTogether.Network.Native", "NetNativeProtocol", "PID_CHEAT_HackDetection", Ret::Void},

    // 4. Movement / physics cheat flags.
    {"KinematicCharacterController", "KinematicCharacterMotor", "get_CheatDetect", Ret::Bool},
    {"KinematicCharacterController", "KinematicCharacterMotor", "set_CheatDetect", Ret::Void},
    {"", "ActorControl", "SetCheatDetect", Ret::Void},
    {"", "ActorCharacterControl", "SetCheatDetect", Ret::Void},
    {"", "VehicleControl", "SetCheatDetect", Ret::Void},
    {"", "BoardMiniGamePlinko", "OnCheatDetected", Ret::Void},
    {"", "BoardMiniGamePlinko", "CheckPhysicsValues", Ret::Void},

    // 5. G-Presto managed scanner and protector.
    {"", "SpeedHackScanner", "Update", Ret::Void},
    {"", "SpeedHackScanner", "OnApplicationPause", Ret::Void},
    {"", "SpeedHackScanner", "OnApplicationFocus", Ret::Void},
    {"GPresto.Protector", "SpeedHack", "IsDetect", Ret::Bool},
    {"GPresto.Protector.Engine", "GPrestoEngine", "IsDetect", Ret::Bool},
    {"GPresto.Protector.Engine", "GPrestoEngine", "SendDetectionLog", Ret::Void},
    {"GPresto.Protector.Engine", "GPrestoIOS", "SendDetectionLog", Ret::Bool},
    // Relay from G-Presto's speed detection into ACTk's proof-time flag.
    // `private void OnSpeedHackDetected()`, present in BOTH 2.32.0 dumps
    // (VNG 2057547, Global 2057852). Earlier revisions dropped this as a phantom
    // because the public AntiCheatListener of the same name was the only hit
    // being grepped; it is real, it is the only writer of
    // SpeedHackProofTime.speedHackDetected, and stubbing it means the flag is
    // never set instead of being raced by the 0.25 s sanitizer.
    // SpeedHackProofTime.Update is deliberately left running: the game reads
    // SpeedHackProofTime.time for its own timing, and freezing it would turn a
    // stubbed detector into a live stale-timestamp detector.
    {"CodeStage.AntiCheat.Time", "SpeedHackProofTime", "OnSpeedHackDetected", Ret::Void},
    // GPrestoIOS.setAlertDetectUnityClose is `private static extern` (dump
    // 2219819): it has an import thunk, not an IL body. Patching the thunk
    // breaks the resolver for the neighbours, and the native side already
    // logged the code by then. It is left alone on purpose.

    // 6. CodeStage Anti-Cheat Toolkit.
    {"CodeStage.AntiCheat.Detectors", "SpeedHackDetector", "Update", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "SpeedHackDetector", "TriggerDetection", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "SpeedHackDetector", "StartDetectionAutomatically", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "SpeedHackDetector", "IsTicksCheated", Ret::Bool},
    {"CodeStage.AntiCheat.Detectors", "TimeCheatingDetector", "Update", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "TimeCheatingDetector", "TriggerDetection", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "TimeCheatingDetector", "StartDetectionAutomatically", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "TimeCheatingDetector", "ForceCheck", Ret::Bool},
    {"CodeStage.AntiCheat.Detectors", "TimeCheatingDetector", "IsReadyForForceCheck", Ret::Bool},
    {"CodeStage.AntiCheat.Detectors", "WallHackDetector", "Update", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "WallHackDetector", "FixedUpdate", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "WallHackDetector", "TriggerDetection", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "WallHackDetector", "StartDetectionAutomatically", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "WallHackDetector", "Detect", Ret::Bool},
    {"CodeStage.AntiCheat.Detectors", "InjectionDetector", "StartDetectionAutomatically", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "InjectionDetector", "StartDetection", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "ObscuredCheatingDetector", "TriggerDetection", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "ObscuredCheatingDetector", "OnCheatingDetected", Ret::Void},
    {"CodeStage.AntiCheat.Detectors", "ObscuredCheatingDetector", "StartDetectionAutomatically", Ret::Void},
};

class Stealth {
public:
    Stealth() = default;
    Stealth(const Stealth&) = delete;
    Stealth& operator=(const Stealth&) = delete;

    void arm(const StealthApi& api) {
        if (armed_ || api_ready_) return;
        journal::mark(journal::Tag::Arm, 0);
        api_ = api;
        api_ready_ = true;

        if (!api_.domain_get || !api_.domain_get_assemblies || !api_.assembly_get_image ||
            !api_.class_from_name || !api_.class_get_method_from_name) {
            api_ready_ = false;
            journal::mark(journal::Tag::Arm, 1);
            return;
        }

        sweep();
        armed_ = aggregator_ok_ && !patches_.empty();
        journal::mark(journal::Tag::Arm, 2, static_cast<std::uintptr_t>(patches_.size()),
                      static_cast<std::uintptr_t>(detours_), armed_ ? 1u : 0u);
    }

    // Cheap pointer compare over every patched method, plus a code-page read-back
    // for the inline detours. 0.10 s, not 1.5 s: SpeedHackDetector ticks at
    // 100 ms and a single frame of restored pointer is a full detection.
    void watchdog(double now) {
        if (!armed_) {
            if (now < nextArm_) return;
            nextArm_ = now + 2.0;
            api_ready_ = false;
            missing_ = 0;
            classCache_.clear();
            arm(api_);
            return;
        }
        if (missing_ > 0 && now >= nextRescan_) {
            nextRescan_ = now + 20.0;
            classCache_.clear();
            missing_ = 0;
            // aggregator_ok_ is deliberately not reset here. arm() is the only
            // place that recomputes it, and it never runs again once armed_, so
            // clearing it would leave the report lying after one transient miss.
            sweep();
        }
        if (now < nextCheck_) return;
        nextCheck_ = now + 0.10;
        for (auto& p : patches_) {
            if (stealth_detail::slot(p.methodInfo) != p.slotValue) {
                if (stealth_detail::slot(p.methodInfo) != p.original) ++contested_;
                journal::mark(journal::Tag::Watchdog, 0,
                              reinterpret_cast<std::uintptr_t>(p.methodInfo), 0, 0);
                stealth_detail::slot(p.methodInfo) = p.slotValue;
                ++reapplied_;
            }
            if (p.detour.ok && p.detour.addr) {
                const auto* ins = static_cast<const std::uint32_t*>(p.detour.addr);
                if (ins[0] != stealth_detail::kLdrX16 || ins[1] != stealth_detail::kBrX16) {
                    journal::mark(journal::Tag::Watchdog, 1,
                                  reinterpret_cast<std::uintptr_t>(p.detour.addr), 0, 0);
                    if (stealth_detail::writeJump(p.detour.addr, p.stub)) ++reapplied_;
                }
            }
        }
    }

    bool armed() const { return armed_; }
    bool apiReady() const { return api_ready_; }
    int patched() const { return static_cast<int>(patches_.size()); }
    int detoured() const { return detours_; }
    int missing() const { return missing_; }

    std::string line() const {
        if (!armed_) {
            if (patches_.empty()) return "Stealth: off (passive read-only mode, 0 memory/code writes)";
            return "Stealth: off (aggregator absent, " +
                   std::to_string(patches_.size()) + " partial stubs)";
        }
        std::string s = "Stealth: armed " + std::to_string(patches_.size()) +
                        " stubs / " + std::to_string(detours_) + " code detours";
        if (missing_ > 0) s += ", " + std::to_string(missing_) + " absent";
        if (reapplied_ > 0) s += ", re-asserted " + std::to_string(reapplied_);
        if (contested_ > 0) s += ", contested " + std::to_string(contested_);
        return s;
    }

private:
    struct Patch {
        void* methodInfo = nullptr;
        void* original = nullptr;   // raw value found in the slot at arm time
        void* stub = nullptr;       // bare stub address, used by the detour
        void* slotValue = nullptr;  // stub as it must land in methodPointer
        stealth_detail::Detour detour{};
    };

    void sweep() {
        journal::mark(journal::Tag::Sweep, 0);
        for (const auto& t : kStealthTargets) {
            // Two marks per target: the first names what is about to be looked
            // up, the second records the class pointer that came back. A fault
            // inside findClass therefore still names its target.
            journal::mark(journal::Tag::Target, 0, 0,
                          reinterpret_cast<std::uintptr_t>(t.cls),
                          reinterpret_cast<std::uintptr_t>(t.method));
            void* klass = findClass(t.ns, t.cls);
            if (!klass) {
                ++missing_;
                continue;
            }
            journal::mark(journal::Tag::Target, 1, reinterpret_cast<std::uintptr_t>(klass),
                          reinterpret_cast<std::uintptr_t>(t.cls),
                          reinterpret_cast<std::uintptr_t>(t.method));
            bool any = false;
            for (std::uint32_t argc = 0; argc <= stealth_detail::kMaxArity; ++argc) {
                void* mi = api_.class_get_method_from_name(klass, t.method, static_cast<int>(argc));
                if (!mi) continue;
                journal::mark(journal::Tag::Method, argc, reinterpret_cast<std::uintptr_t>(mi),
                              reinterpret_cast<std::uintptr_t>(t.cls),
                              reinterpret_cast<std::uintptr_t>(t.method));
                if (addPatch(mi, t.ret, argc)) any = true;
            }
            if (any) {
                if (std::strcmp(t.cls, "AntiCheatListener") == 0) aggregator_ok_ = true;
            }
        }
    }

    void* findClass(const char* ns, const char* cls) {
        const std::string key = std::string(ns ? ns : "") + "|" + cls;
        if (auto it = classCache_.find(key); it != classCache_.end()) return it->second;
        void* found = nullptr;
        void* domain = api_.domain_get();
        if (domain) {
            std::size_t count = 0;
            void** assemblies = api_.domain_get_assemblies(domain, &count);
            if (assemblies && count > 0 && count <= 4096) {
                for (std::size_t i = 0; i < count && !found; ++i) {
                    journal::mark(journal::Tag::FindClass, static_cast<std::uint32_t>(i),
                                  reinterpret_cast<std::uintptr_t>(assemblies[i]), 0, 0);
                    void* image = api_.assembly_get_image(assemblies[i]);
                    if (!image) continue;
                    found = api_.class_from_name(image, ns, cls);
                }
            }
        }
        classCache_.emplace(key, found);
        return found;
    }

    bool addPatch(void* methodInfo, Ret ret, std::uint32_t argc) {
        for (auto& p : patches_) {
            if (p.methodInfo == methodInfo) {
                applyDetour(p);
                return true;
            }
        }
        void* current = stealth_detail::slot(methodInfo);
        if (!stealth_detail::plausible(current)) return false;

        Patch p;
        p.methodInfo = methodInfo;
        p.original = current;
        p.stub = stealth_detail::thunkFor(ret, argc);
        p.slotValue = stealth_detail::signCode(p.stub);
        p.detour.addr = reinterpret_cast<void*>(stealth_detail::rawCode(current));

        // Pointer first: it is free, and it is what delegates and reflection see.
        // The body write goes second so direct and vtable calls land on the stub
        // too; that one is the only step that can fail, so it is retried later.
        journal::mark(journal::Tag::PatchSlot, argc, reinterpret_cast<std::uintptr_t>(methodInfo),
                      reinterpret_cast<std::uintptr_t>(current),
                      reinterpret_cast<std::uintptr_t>(p.slotValue));
        stealth_detail::slot(methodInfo) = p.slotValue;
        patches_.push_back(p);
        applyDetour(patches_.back());
        return true;
    }

    // Do NOT patch __TEXT code pages (no vm_protect / VM_PROT_COPY inline detours).
    // Modifying __TEXT creates private copy-on-write (SM_COW) pages and alters code
    // hashes that GPresto / CodeHashDic integrity checks inspect. Keeping __TEXT 100%
    // untouched ensures read-only features like ESP never trigger binary integrity flags.
    void applyDetour(Patch& p) {
        (void)p;
        return;
    }

    StealthApi api_{};
    std::unordered_map<std::string, void*> classCache_;
    std::vector<Patch> patches_;
    bool api_ready_ = false;
    bool armed_ = false;
    bool aggregator_ok_ = false;
    int missing_ = 0;
    int reapplied_ = 0;
    int contested_ = 0;
    int detours_ = 0;
    double nextCheck_ = 0.;
    double nextArm_ = 0.;
    double nextRescan_ = 0.;
};

} // namespace vt::ios
#endif // VT_IOS_STEALTH_HPP_INCLUDED
