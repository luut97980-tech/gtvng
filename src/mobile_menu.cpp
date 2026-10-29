#include "mobile_menu.hpp"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace vt::ios {
namespace {
constexpr ImVec4 accent{0.24f, 0.64f, 1.0f, 1.0f};
float finite(float v, float def, float min, float max) {
    return std::isfinite(v) ? std::clamp(v, min, max) : def;
}
bool button(const Model& m, const char* label, ImVec2 size) {
    const bool pressed = ImGui::Button(label, size);
    return pressed && !m.suppressActions;
}
void feature(Model& m, const Actions& a, int id) {
    if (id < 0 || id >= FeatureCount) return;
    const char* titles[] = {"ESP", "Line", "Spam nhảy", "Tự sẵn sàng", "Tự động búa"};
    ImGui::PushID(id);
    const bool enabled = m.available[id] && m.connected && m.controlsAllowed && m.license.valid;
    ImGui::BeginDisabled(!enabled);
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr float rowH = 24.f;
    const bool pressed = ImGui::InvisibleButton(titles[id], {width, rowH}) && !m.suppressActions;
    auto* d = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) d->AddRectFilled(p, {p.x + width, p.y + rowH}, IM_COL32(34, 36, 40, 255), 5);
    d->AddText({p.x + 2, p.y + (rowH - ImGui::GetFontSize()) * .5f}, enabled ? IM_COL32(236, 239, 244, 255) : IM_COL32(136, 142, 154, 255), titles[id]);
    const ImVec2 q{p.x + width - 46, p.y + (rowH - 22.f) * .5f};
    d->AddRectFilled(q, {q.x + 44, q.y + 22}, (m.enabled[id] && enabled) ? IM_COL32(61, 163, 255, 255) : IM_COL32(62, 65, 71, 255), 11);
    d->AddCircleFilled({q.x + ((m.enabled[id] && enabled) ? 32.f : 12.f), q.y + 11}, 8.5f, IM_COL32(247, 249, 252, enabled ? 255 : 160));
    ImGui::EndDisabled();
    if (pressed && a.toggle && enabled) a.toggle(id);
    ImGui::PopID();
}
void interval(Model& m, const Actions& a) {
    ImGui::TextUnformatted("Nhịp nhảy"); float v = float(m.jumpIntervalMs); ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::SliderFloat("##interval", &v, 0, 1000, "%.0f ms", ImGuiSliderFlags_NoInput);
    m.sliderActive = m.sliderActive || ImGui::IsItemActive();
    if (changed && !m.suppressActions) { m.jumpIntervalMs = std::floor(v); if (a.number) a.number(SpamJump, m.jumpIntervalMs); }
}
void hammerDelay(Model& m, const Actions& a) {
    ImGui::TextUnformatted("Độ trễ búa");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 54.f);
    if (button(m, "0 ms##HammerZero", {54.f, 20.f})) {
        m.hammerDelayMs = 0.;
        if (a.number) a.number(AutoHammer, m.hammerDelayMs);
    }
    float v = float(std::clamp(m.hammerDelayMs, 0., 1000.));
    ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::SliderFloat("##hammer_delay", &v, 0, 1000, "Sau FIGHT: %.0f ms", ImGuiSliderFlags_NoInput);
    m.sliderActive = m.sliderActive || ImGui::IsItemActive();
    if (changed && !m.suppressActions) {
        m.hammerDelayMs = std::floor(v);
        if (a.number) a.number(AutoHammer, m.hammerDelayMs);
    }
}
} // namespace

void sanitize(Model& m) {
    m.scale = finite(m.scale, 1, .85f, 1.25f);
    m.opacity = finite(m.opacity, .96f, .65f, 1.f);
    m.jumpIntervalMs = std::isfinite(m.jumpIntervalMs) ? std::clamp(m.jumpIntervalMs, 0., 1000.) : 20.;
    m.hammerDelayMs = std::isfinite(m.hammerDelayMs) ? std::clamp(m.hammerDelayMs, 0., 10000.) : 0.;
}

bool acceptsPoint(const Model& m, ImVec2 p) {
    return p.x >= m.windowMin.x && p.y >= m.windowMin.y && p.x < m.windowMax.x && p.y < m.windowMax.y;
}

void style() {
    ImGui::StyleColorsDark(); auto& s = ImGui::GetStyle();
    s.WindowMinSize = {20, 20}; s.WindowRounding = 8; s.ChildRounding = 0; s.FrameRounding = 5; s.GrabRounding = 5; s.ScrollbarRounding = 5;
    s.WindowPadding = {12, 12}; s.FramePadding = {8, 5}; s.ItemSpacing = {6, 4}; s.ScrollbarSize = 12;
    s.WindowBorderSize = 1; s.Colors[ImGuiCol_WindowBg] = {.065f, .075f, .09f, 1};
    s.Colors[ImGuiCol_ChildBg] = {0, 0, 0, 0}; s.Colors[ImGuiCol_Border] = {.22f, .25f, .29f, 1};
    s.Colors[ImGuiCol_Button] = {.17f, .19f, .22f, 1}; s.Colors[ImGuiCol_ButtonHovered] = {.22f, .27f, .33f, 1};
    s.Colors[ImGuiCol_ButtonActive] = {.24f, .36f, .49f, 1}; s.Colors[ImGuiCol_CheckMark] = accent;
    s.Colors[ImGuiCol_SliderGrab] = accent; s.Colors[ImGuiCol_SliderGrabActive] = {.54f, .78f, 1, 1};
    s.Colors[ImGuiCol_FrameBg] = {.15f, .18f, .22f, 1}; s.Colors[ImGuiCol_Text] = {.93f, .94f, .96f, 1};
    s.Colors[ImGuiCol_TextDisabled] = {.53f, .57f, .63f, 1};
}

void draw(Model& m, const Actions& a) {
    sanitize(m); auto& io = ImGui::GetIO(); io.MouseDrawCursor = false; io.FontGlobalScale = m.scale;
    m.sliderActive = false; ImGui::GetStyle().Alpha = m.opacity;
    const ImVec2 origin = m.insetMin;
    const ImVec2 available = {io.DisplaySize.x - origin.x - m.insetMax.x, io.DisplaySize.y - origin.y - m.insetMax.y};

    if (m.time < m.toastUntil && !m.toast.empty()) {
        auto* d = ImGui::GetForegroundDrawList(); const ImVec2 p{origin.x + 12, origin.y + 12}; auto size = ImGui::CalcTextSize(m.toast.c_str());
        d->AddRectFilled(p, {p.x + size.x + 20, p.y + size.y + 14}, IM_COL32(24, 31, 40, 240), 5);
        d->AddText({p.x + 10, p.y + 7}, IM_COL32(213, 232, 255, 255), m.toast.c_str());
    }

    if (available.x < 200 || available.y < 160) {
        m.windowMin = m.windowMax = {};
        return;
    }
    const bool landscape = available.x > available.y;

    // 1. Collapsed Floating Menu Button (Compact & Draggable anywhere on screen)
    if (!m.visible) {
        const ImVec2 size{landscape ? 62.f : 58.f, landscape ? 28.f : 30.f};
        const float maxBtnX = std::max(origin.x, origin.x + available.x - size.x);
        const float maxBtnY = std::max(origin.y, origin.y + available.y - size.y);
        if (m.floatingButtonPos.x < 0) {
            m.floatingButtonPos = {origin.x + 10.f, origin.y + std::max(10.f, available.y - size.y - 10.f)};
        }
        m.floatingButtonPos.x = std::clamp(m.floatingButtonPos.x, origin.x, maxBtnX);
        m.floatingButtonPos.y = std::clamp(m.floatingButtonPos.y, origin.y, maxBtnY);

        if (std::fabs(m.floatingButtonPos.x - m.prevFloatingButtonPos.x) > 0.5f ||
            std::fabs(m.floatingButtonPos.y - m.prevFloatingButtonPos.y) > 0.5f) {
            ImGui::SetNextWindowPos(m.floatingButtonPos, ImGuiCond_Always);
            m.prevFloatingButtonPos = m.floatingButtonPos;
        }
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {3, 3});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2, 2});
        ImGui::Begin("##Open", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 curBtnPos = ImGui::GetWindowPos();
        if (std::fabs(curBtnPos.x - m.floatingButtonPos.x) > 0.5f || std::fabs(curBtnPos.y - m.floatingButtonPos.y) > 0.5f) {
            m.floatingButtonPos = curBtnPos;
            m.prevFloatingButtonPos = curBtnPos;
            if (a.save) a.save();
        }
        m.windowMin = curBtnPos;
        m.windowMax = {m.windowMin.x + size.x, m.windowMin.y + size.y};
        if (button(m, "Menu", {size.x - 6.f, size.y - 6.f})) {
            m.visible = true; m.suppressActions = false;
        }
        ImGui::End(); ImGui::PopStyleVar(3);
        return;
    }

    // 2. Main Menu Window (Draggable by title/header anywhere on screen)
    const float width = std::min(landscape ? std::min(430.f, available.x * .60f) : 380.f, available.x - 16.f);
    const float maxHeight = landscape ? (m.showDetails ? 360.f : (!m.license.valid ? 320.f : 290.f)) :
                                        (m.showDetails ? 600.f : (!m.license.valid ? 390.f : 290.f));
    const float height = std::min(maxHeight, available.y - 16.f);
    const float maxWinX = std::max(origin.x, origin.x + available.x - width);
    const float maxWinY = std::max(origin.y, origin.y + available.y - height);

    if (m.menuWindowPos.x < 0) {
        m.menuWindowPos = {origin.x + (available.x - width) * .5f, origin.y + (available.y - height) * .5f};
    }
    m.menuWindowPos.x = std::clamp(m.menuWindowPos.x, origin.x, maxWinX);
    m.menuWindowPos.y = std::clamp(m.menuWindowPos.y, origin.y, maxWinY);

    if (std::fabs(m.menuWindowPos.x - m.prevMenuWindowPos.x) > 0.5f ||
        std::fabs(m.menuWindowPos.y - m.prevMenuWindowPos.y) > 0.5f) {
        ImGui::SetNextWindowPos(m.menuWindowPos, ImGuiCond_Always);
        m.prevMenuWindowPos = m.menuWindowPos;
    }
    ImGui::SetNextWindowSize({width, height});
    ImGui::Begin("PTG iOS##Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 curWinPos = ImGui::GetWindowPos();
    if (std::fabs(curWinPos.x - m.menuWindowPos.x) > 0.5f || std::fabs(curWinPos.y - m.menuWindowPos.y) > 0.5f) {
        m.menuWindowPos = curWinPos;
        m.prevMenuWindowPos = curWinPos;
        if (a.save) a.save();
    }
    m.windowMin = curWinPos;
    m.windowMax = {m.windowMin.x + width, m.windowMin.y + height};

    // Header (Touch and drag here to move menu)
    ImGui::SetCursorPos({12, 16}); ImGui::TextUnformatted("PTG | R5.9"); ImGui::SameLine();
    ImGui::TextColored(m.connected ? ImVec4(.35f, .8f, .55f, 1) : ImVec4(.72f, .75f, .8f, 1), "%s", m.connected ? "Đã kết nối" : "Chờ");
    ImGui::SetCursorPos({width - 64, 10}); if (button(m, "Ẩn", {52, 32})) m.visible = false;

    ImGui::SetCursorPos({12, 46}); const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float bodyHeight = std::max(34.f, height - 46.f - 48.f);
    ImGui::BeginChild("Content", {contentWidth, bodyHeight}, ImGuiChildFlags_None);
    m.contentMin = ImGui::GetWindowPos(); m.contentMax = {m.contentMin.x + ImGui::GetWindowWidth(), m.contentMin.y + ImGui::GetWindowHeight()};
    if (m.scrollDelta != 0) { ImGui::SetScrollY(std::max(0.f, ImGui::GetScrollY() - m.scrollDelta)); m.scrollDelta = 0; }

    // Key License Section (Hidden on main menu once activated; no server URL/IP exposed)
    if (!m.license.valid) {
        ImGui::TextColored(ImVec4(1.f, .82f, .25f, 1.f), "XÁC THỰC KEY BẢN QUYỀN");
        char keyBoxLabel[128];
        if (m.keyInput[0] != '\0') {
            const std::size_t klen = std::strlen(m.keyInput);
            if (klen > 12) {
                std::snprintf(keyBoxLabel, sizeof(keyBoxLabel), "Key: %.8s••••%s (Chạm để sửa)##KeyInputBox", m.keyInput, m.keyInput + klen - 4);
            } else {
                std::snprintf(keyBoxLabel, sizeof(keyBoxLabel), "Key: %s (Chạm để sửa)##KeyInputBox", m.keyInput);
            }
        } else {
            std::snprintf(keyBoxLabel, sizeof(keyBoxLabel), "Chạm vào đây để Nhập Key##KeyInputBox");
        }
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.14f, .17f, .22f, 1.f));
        if (button(m, keyBoxLabel, {-1, 36})) {
            if (a.promptKeyInput) a.promptKeyInput();
        }
        ImGui::PopStyleColor();

        const float actBtnW = (contentWidth - 8) * 0.5f;
        if (button(m, "Dán từ bộ nhớ", {actBtnW, 36})) {
            if (a.pasteKey) a.pasteKey();
        }
        ImGui::SameLine();
        if (button(m, "Kích hoạt Key", {actBtnW, 36})) {
            if (a.activateKey) a.activateKey(m.keyInput);
        }
        if (!m.license.statusText.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.f, .45f, .45f, 1.f), "%s", m.license.statusText.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::BeginDisabled(true); // Disable features until key is activated
    }

    feature(m, a, Esp);
    feature(m, a, Line);
    feature(m, a, SpamJump);
    feature(m, a, AutoReady);
    feature(m, a, AutoHammer);
    interval(m, a);
    hammerDelay(m, a);

    if (!m.license.valid) {
        ImGui::EndDisabled();
    }

    if (m.showDetails) {
        ImGui::Spacing(); ImGui::Separator();
        if (m.license.valid) {
            ImGui::TextColored(ImVec4(.35f, .92f, .55f, 1.f), "%s", m.license.statusText.c_str());
            if (button(m, "Đổi Key bản quyền", {-1, 32})) {
                if (a.clearKey) a.clearKey();
            }
            ImGui::Spacing();
        }
        const char* antiCaptureLabel = m.antiCapture ? "Ẩn khi quay/chụp: BẬT" : "Ẩn khi quay/chụp: TẮT";
        if (button(m, antiCaptureLabel, {-1, 38})) {
            m.antiCapture = !m.antiCapture;
            if (a.toggleAntiCapture) a.toggleAntiCapture(m.antiCapture);
            if (a.save) a.save();
        }
        if (button(m, "Sao chép chẩn đoán", {-1, 38}) && a.diagnostic) a.diagnostic();
        ImGui::TextWrapped("%s", m.status.c_str());
        ImGui::TextDisabled("Target VNG / Global 2.32.0 (227628)");
        if (!m.player.empty()) ImGui::TextWrapped("UID: %s", m.player.c_str());
        ImGui::TextWrapped("ESP Minigame + Line dẫn đường + Tự nhảy + Tự sẵn sàng + Tự động búa.");
    }
    ImGui::EndChild();

    ImGui::SetCursorPos({12, height - 44.f});
    const float footerWidth = (contentWidth - 12) / 3.f;
    if (button(m, "Lưu", {footerWidth, 36}) && a.save) {
        a.save();
    }
    ImGui::SameLine();
    if (button(m, "Tắt hết", {footerWidth, 36}) && a.stop) {
        a.stop();
    }
    ImGui::SameLine();
    if (button(m, "Chi tiết", {footerWidth, 36})) {
        m.showDetails = !m.showDetails;
    }

    ImGui::End();
}
} // namespace vt::ios
