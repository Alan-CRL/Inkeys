# Win32 source checks (2026-10-17)

- RegisterRawInputDevices: only one receiving window per raw device class per process; last registration wins; RIDEV_REMOVE requires hwndTarget=null. https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices
- RAWINPUTHEADER: hDevice may be zero for precision touchpad input. Never classify every null-device packet as promoted touchscreen input. https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputheader
- Raw input overview: background receipt is enabled with RIDEV_INPUTSINK; it is not a documented blanket guarantee to bypass UIPI/security-desktop boundaries. https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input
- Standard read: parse HRAWINPUT before DefWindowProc cleanup, do not retain handle across threads; copy semantic samples. https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input
- WM_TOUCH routes to initial window greedily until focus changes; keep contact identity and deliberate cancel, don't infer Up from missing move. RegisterTouchWindow is supported on Windows7 and must be called by window owner thread. https://learn.microsoft.com/en-us/windows/win32/wintouch/getting-started-with-multi-touch-messages

## Scope-specific spec precedence
rendering-and-ui.md old third-light contract unregisters mouse raw input on dormancy/canvas Started. User-approved shared input design supersedes that registration behavior: dormancy only disables light tracking/wake, interaction registration remains until sink shutdown. Preserve light fade/timer/wait-for-leave behavior.
