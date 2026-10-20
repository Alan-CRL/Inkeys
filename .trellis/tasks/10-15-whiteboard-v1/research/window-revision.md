# Window revision — 2026-10-15

- Window service is the sole owner of minimized state. Exposed `WhiteboardWindowGroupMinimized`; stale show commands cannot resurrect canvas or page controls. Freeze rendering/bounds skip minimized anchor. Bar remains visible after Windows owned-popup minimization; page/editor runtime is untouched.
- Freeze spec callbacks enforce live PPT minimize prohibition at command execution (including system SC_MINIMIZE), and wake PageControl, Bar, and background render clients on state edges. Window module retains no UI dependency. PageControl owner uses getter to suppress surfaces; Bar owner handles taskbar-centered placement.
- Freeze title uses localized Inkeys Whiteboard / Inkeys 白板; large/small HWND and class icons use application IDI_ICON1. Added Previous/Next/Add page labels for pagination owner.
- Updated HWND window regression for PPT rejection, minimized getter, late Show suppression, and visible toolbar; not executed because current authorization is no-window only. Parent will build solution and run allowed tests. i18n sync/check passes 368/368 translations per locale; git diff --check passes.
- Real taskbar icon, owned popup visibility after minimizing Freeze, restore and taskbar placement remain manual GUI acceptance; no GUI launched.
