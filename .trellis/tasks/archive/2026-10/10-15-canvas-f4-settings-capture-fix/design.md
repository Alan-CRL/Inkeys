# F4：设置隐藏时收束鼠标捕获：设计

## Change Boundary

F4已证隐藏Settings保留capture并阻断Bar点击，同场新笔可用。修复Setting专用native Hide事务及旧输入session，保留共享UI渲染/窗口线程分工和现有锁环防护。

## Selected Design

Window Service的Setting owner执行Hide、退出隐藏组、Show期间shutdown隐藏回退时，复用小型私有清理：仅GetCapture()==该Setting HWND才ReleaseCapture，复查后再隐藏；已归其他HWND时不释放。重复隐藏也检查，不以可见性跳过。该保护不推广到全部角色。

ReleaseCapture同步产生WM_CAPTURECHANGED。Setting WndProc在visible guard/settingImguiMutex前轻量返回处理该通知，不触碰context、不抢capture、不发RTS Cancel，也不把普通Up后的capture changed都标为重建。

SessionState锁存Hide失效，快速Hide→Show不能清掉。Resolve对旧session要求Release，最终可见时再Rebuild；旧CommitEpoch不得抹去Resolve后新到Hide。失效只在旧session已由render Drain销毁，或确认无旧session并准备首次新建的边界消费，避免无意义二次重建。

旧backend/context继续由DrainSettingSessionOnRenderThread及现有render owner销毁/重建；owner清理不等待render、不访问已销毁context，状态短锁不跨Drain。无需修改vendor、伪造鼠标Up或新增公开API。

## Expected Files

- Inkeys/Inkeys/Window/Window.cpp：Setting专用capture清理与三个隐藏落点。
- Inkeys/Inkeys/UI/Setting/Setting.cpp：WM_CAPTURECHANGED早处理、会话新建/Drain边界接线。
- Inkeys/Inkeys/UI/Setting/Setting.SessionState.h：Hide失效锁存和消费。
- InkeysHeadlessTests/setting_session_state_tests.cpp、window_tests.cpp：真实状态对象及WindowService边界回归；无需新项目/CLI。

## Tests and Limits

无窗口SessionState覆盖Hide→Show合并、重复Hide、无session、新建失败、Resolve后新Hide/旧CommitEpoch、Release和epoch/resize旧合同。真实WindowService在所属线程建立capture再执行service.Hide，检查Setting捕获释放及其他HWND捕获不受影响，覆盖owned/unowned/重复Hide/Shutdown。

完整修复构建必须实际运行Settings内容区Down→Alt+F4→正常Up→原Bar橡皮坐标点击；记录capture、工具业务状态及新笔迹。补普通X、重开第一次点击、快速Hide→Show和按住时退出。WindowService测试的测试WndProc不等于完整Settings/ImGui验收；--no-window也不运行这些HWND测试。

## Dependencies and Rollback

功能无其他子任务依赖；GUI与其他项串行，开启诊断的集成验收待F1合入。按本项三产品文件完整回退，保留他人修改，不牵涉RTS输入恢复、Z序或DXGI猜测性修复；不commit/push。

专项依据：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/research/f4-fix-plan.md`。本方案已获父最终摘要审批，当前进入实施。
