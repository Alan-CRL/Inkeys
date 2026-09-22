# 定格 Magnification 请求收敛与失败处理

## Background

- 基线提交 `ba14ddd6f4fa878cb2b51f791363dae48e6ee148` 已将 Magnification 排除列表改为在每次 source 更新前从 Window Service 重建、校验、去重并提交。
- 用户已人工确认原先主栏等 Inkeys 窗口被固化进定格画面的现象似乎解决；本轮必须保留该修复，不能恢复旧的 HWND 快照或 legacy 句柄来源。
- 当前 `FreezeFrameWindow()` 与 `MagnifierThread()` 都会独立更新并显示 Magnifier；更新失败也可能被调用方后续的重绘、alpha=255 和状态提交覆盖。
- `RequestUpdateMagWindow` 与 `magnificationReady` 还是跨线程普通变量，布尔请求无法区分“开启→关闭→再次开启”的不同用户意图，也无法让迟到结果安全失效。

## Goal

在不替换 Magnification 后端、不改变窗口层级和捕获尺寸的前提下，将桌面定格的更新、显示和结果提交收敛为单一可取消执行路径，使失败不会伪装成成功，旧请求不会在取消、工作区切换或退出后重新揭开定格窗口。

## Requirements

### 单一执行路径

- 同一次有效开启请求只允许一次 `MagSetWindowSource` 提交。
- 排除列表提交、source 提交、必要重绘、Magnifier Host/Child 显隐与 alpha、执行结果提交由同一协调路径负责。
- `FreezeFrameWindow()` 只保留恢复/PPT 等既有提示绘制，不再直接更新或显示 Magnifier，也不再额外发布第二次更新请求。
- 旧画面不能在新 source 成功提交前以不透明状态提前显露。

### 有序请求与取消

- 区分用户期望状态、请求版本和已经应用状态；不能用单一布尔值代表全部三者。
- 用户开启、关闭、再次开启必须产生不同版本；可合并尚未执行且已经过期的请求。
- 准备或提交期间发生取消、PPT/白板切换、停止或退出时，旧请求不能在完成后恢复显示。
- 失败只影响对应版本；不得用无条件 `Freeze::Toggle()` 回滚并意外反向开启。
- HWND 与停止顺序必须清楚，不得在 Window Service 销毁窗口后继续使用旧句柄，也不得持业务锁同步等待窗口线程回调同一把锁。

### 失败贯穿到显示结果

- 更新操作返回 `[[nodiscard]]` 的明确结果，并区分过滤、source、重绘、显示/alpha、句柄或取消等失败阶段。
- 过滤提交失败时不得调用本次 `MagSetWindowSource`；source 或后续必要步骤失败时不得揭开画面或提交“已应用成功”。
- 窗口显示、隐藏、重绘与 alpha 等必要 Win32 操作必须检查结果；内部状态只能在实际操作成功后更新。
- API 成功日志只描述“更新已提交/显示已应用”，不得声称已验证像素或 GPU 首帧正确。
- 单次失败不能永久封死功能；后续新的有效请求仍可重新尝试。持续相同失败需要去重，恢复时可记录一次。

### 排除列表

- 保留 Window Service 作为当前角色 HWND 的唯一事实源。
- 角色继续覆盖 `MagnifierHost`、`Freeze`、`DrawpadPresentation`、`Drawpad`、四个 PPT 角色、`Bar` 和存在时的 `Setting`。
- 保留非空、`IsWindow`、当前进程、非 `WS_CHILD`、按 HWND 去重；Setting 不存在是正常情况。
- 每次 source 更新前提交完整当前集合；不得依赖 owner 隐式排除其他窗口，也不得排除第三方窗口。
- 本次执行使用的 Host/Child 必须与 Window Service 当前有效目标一致；`IsWindow` 只作为瞬时校验，不当作生命周期保证。

### 诊断

- 删除 `D3DDEVCAPS_HWTRANSFORMANDLIGHT => WDDM 支持` 的错误推断；若 D3D9 只服务于该诊断则移除相关依赖。
- 日志包含准确失败阶段、必要目标 HWND 和请求标识。没有文档保证扩展错误码的 API 不把 `GetLastError` 写成确定根因。

## Non-goals

- 不处理 dGPU/iGPU、混合显卡、GPU 偏好、WOW64 或 Win7 专项兼容问题。
- 不增加辅助进程，不替换 Magnification，不引入 WGC、Desktop Duplication、私有 DWM API 或新的取图后端。
- 不使用 `WDA_EXCLUDEFROMCAPTURE`，不通过临时隐藏主栏或主画布截图。
- 不改变主画布绘图设备、笔迹渲染、输入架构、overlay owner 链、焦点、任务栏或 topmost 规则。
- 不修改捕获尺寸和保留一像素策略；桌面定格不得重新调用 `SetOverlayFullscreen`，白板既有行为不变。
- 保留定格开启时的即时根置顶刷新。
- 不用固定 Sleep、延长轮询、`volatile` 或持续重复抓取掩盖时序问题。

## Acceptance Criteria

- [x] 基线动态排除列表修复保留，且用户已初步人工确认主栏残影问题得到改善。
- [x] 一次有效开启请求只执行一次正常 source 更新，Magnifier 更新/显示只有一个生产执行入口。
- [x] 过滤失败不调用 source、不揭开画面、不标记已应用；source 或必要显示步骤失败同样不能走成功路径。
- [x] 首次失败后后续有效请求可以重新执行并成功收敛。
- [x] 开启→关闭→开启、准备中取消和旧结果晚到均以最新请求为准。
- [x] PPT/白板切换、停止和退出会使旧请求失效，不能重新显示 Magnifier。
- [x] 排除列表继续过滤空、销毁、其他进程、child、重复 HWND，Setting 缺失正常；句柄变化后新请求使用当前集合。
- [x] 错误的 D3D9/WDDM 诊断已删除，失败/恢复日志准确且去重。
- [x] 针对性测试覆盖生产使用的请求协调、失败传播和过滤逻辑，而不是复制算法自测。
- [x] `InkeysRepo.sln` 的 `Debug | ARM64` 完整构建、`InkeysHeadlessTests.exe --no-window` 和 `git diff --check` 通过。
- [x] 真实 Magnification 画面、快速开关、设置窗/模式/PPT/白板切换及任务栏/置顶/焦点/一像素行为由用户人工验收。

## Deferred Investigation

此前关于 Magnification/WOW64、Win7、混合显卡和替代捕获后端的调查保留在 `research/`。本轮不以并未验证的 GPU 推断扩大实现范围。
