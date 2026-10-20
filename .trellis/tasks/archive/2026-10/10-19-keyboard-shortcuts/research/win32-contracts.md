# Win32 官方合同核对（仅规划）

- RegisterHotKey：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey 。可用NULL HWND注册到调用线程队列；必须处理WM_HOTKEY、检查返回值和GetLastError；MOD_NOREPEAT防长按重复，Vista不支持该标志，本项目Win7最低声明可采用。更改/停止显式注销，失败不静默换钩子强抢。
- LowLevelKeyboardProc：https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc 。安装线程需要消息泵；负code直接CallNext。回调在异步键态更新前执行，不可用GetAsyncKeyState读当前事件的新键态；应专用线程快速交给consumer。Win7后超时可静默移除，不把超时上限当性能预算。
- KBDLLHOOKSTRUCT：https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-kbdllhookstruct 。值含vkCode/scanCode/flags/time/extraInfo，区分系统键、Up、extended、injected/lowerIL；复制值，不持系统指针。
- GetAsyncKeyState：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate 。UIPI、非活动桌面等可导致0，不能据此制造modifier释放或认定所有键均Up。
- GetDoubleClickTime：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdoubleclicktime 。是鼠标双击时间，不自动成为键盘序列需求。本期提议同一个连续Ctrl会话两个独立Delete边沿，不额外引入500ms时间窗。

设计推论（不是API承诺）：全局注册组合在系统层被保留，收到后忽略WM_HOTKEY不等于原键已交还文本输入；专用X全局键与普通功能键作用域必须分别说明。AltGr可能与Ctrl+Alt组合重叠，注册键后不能靠后验hook吞/重放恢复文字，不能承诺跨键盘布局或UIPI均成功。拟保留单独开关和注册失败状态，无模拟补键或静默提升权限。

本轮只读官方文档；未注册系统热键/钩子、未做Win7/管理员窗口实机测试。
