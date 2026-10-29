# Lumin adaptation

Upstream: https://github.com/poncippg-spec/Lumin-Free-Imgui-Menu
Original Lumin code and project files: MIT, Copyright (c) 2026 Lumin contributors.
See LICENSE alongside this notice.

This frontend adapts Lumin's dark palette, rounded sidebar/panels, row switches
and slider treatment to the existing VT ImGui 1.91.9b renderer and MenuActions.
The palette is taken from framework/settings/colors.h (archived alongside).
Widgets are rewritten for the current public ImGui API and input contract.
It does not replace the current license checks with upstream's demo login,
import upstream executables/prebuilt libraries, or use its Flaticon icon font.
Icons are simple geometry drawn by the frontend. Existing Segoe UI on Windows
is used; the review preview uses DejaVu Sans.
