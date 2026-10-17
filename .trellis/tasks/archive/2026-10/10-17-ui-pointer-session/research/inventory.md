# Read-only investigation carried into implementation

Bar continuous consumers: Seek main-window movement/dock; swatch indices 1..11 drag-to-select; 2D RunColorPickerPointerGesture; thickness track/thumb; touch preview relative drag; slider numerical popup; FineDial numerical-popup return; FineDial body and dwell/drag-zone activation. Preserve immediate swatch apply and per-control commit/cancel. Ordinary button loops also need reliable lifecycle.

Raw registration currently controlled by SetBorderCursorRawInputEnabled and edge-light dormancy. It must no longer unregister input required by interaction. WM_INPUT currently records cursor activity only. HiMsg remains a generic unchanged library. Touch markers live in ExMessage.wheel for non-wheel events; screen coordinate handling is already special to avoid moving-Bar coordinate drift.

PageControl contains four PPT HWNDs, bottom pair and middle pair. PPT background/drag handle can move; Whiteboard input policy does not grant the same drag area. Preserve rounded-background hit and transparent margins. Owner WndProc direct movement must avoid blocking the rendering thread: inspect current try_lock/latest-wins pair commit contract in specs. Existing long press uses capture checks; completion/cancellation should be source-session based when raw owns the gesture.

No hook changes. Active MouseHook updates global caches and handles other product behavior; this task makes UI gestures independent without editing, removing or reinstalling it. IsKeyBoardDown is a cache accessor, not a direct Win32 API. Draw3's remaining GetAsyncKeyState mouse-button classification is out of scope.
