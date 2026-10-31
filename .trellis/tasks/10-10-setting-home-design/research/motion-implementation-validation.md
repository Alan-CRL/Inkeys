# 第二轮主页布局与轻量动效验收记录

日期：2026-10-10。当前工作区/分支 fluent；继续 10-10-setting-home-design，父任务保持原关联。本记录对应 follow-up-plan.md，第一轮 implementation-validation.md 为历史证据。

## 实现与独立审查

- trellis-implement 完成统一渐变横幅、居中开源区域、自适应同字号标语、删除横幅旧按钮/主页教程展开区、紧凑版权及透明字标动效；探索/常用设置/作者等后续区域保持原布局。
- HomeMotionState 由常驻设置会话持有；ShowSerial 只表示真实隐藏到显示，配合真实导航目的地判断进入。原设置动画时钟、开关、速率与输入/窗口门禁复用。
- 原始字标网格预缓存，视差与中心向外反光只修改已有顶点的投影/RGB，AA alpha 保留；稳定结果复用。按钮图标一次性反馈不改变命中区域。产品无新增 shader、离屏纹理、设备、线程、定时器或额外渲染请求。
- trellis-check 独立审查无未修复产品缺陷；加强非鼠标输入、首次 AutoResizeY 进入及离屏回归。详见 motion-check-review.md。
- Scripts/i18n.ps1 的两处最小条件调整支持基准为空的显式空翻译。SC/TC HeroJoin 为空、EN 为空格；sync 后 check 两语言各 384/384。真实脚本函数 9-case 空值回归全部通过，缺失/普通空文案/源变更/待翻译标记仍被检测。

## 编译与测试

- 首轮完整 InkeysRepo.sln Debug|ARM64 构建 ExitCode=0，282.284 秒；最终同一 solution 增量构建 ExitCode=0，26.566 秒。
- ARM64 原生 MSBuild 动态由 vswhere 定位：`C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`。同一 PowerShell invocation 应用 PATH workaround、禁用 node reuse，构建/测试均配置 1800 秒上限。现有 hashlib++ / HiMsg 依赖警告未越界修改。
- 最终 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` ExitCode=0，156.923 秒，stderr 为空。包含真实三语言/字体、七倍率（1/1.25/1.5/1.875/2/3/4）、浅深、导航展开/收起、三窗口与额外窄宽共 420 布局组合；原滚动、控件与输入回归继续运行。
- 真实 tap、Tab/Enter/Space、拖动取消、GPL/GitHub/GitCode/Support 动作、稳定命中区域、四角投影和透明 AA、切页/ShowSerial/重复当前主页/离屏/失焦/动画开关与速率路径均通过。测试仅检查动作返回/路由，不发起外链。

## 失败定位与修正记录

1. 首轮 headless ExitCode=1、10 项失败：dark 对照 outside 固定 114932，原 4+1 帧主题预热不足；现有 Fluent AnimateColorU32 默认 0.083 秒指数逼近，尚未稳定。基线改为至少 21 帧且连续 3 次真实全画面读回完全一致；最多 120 帧，不放宽 outside==0、仅提亮或最终像素完全复原断言。
2. 同轮两项离屏失败：新紧凑布局的 800 DIP 视口实际 scroll max 不足以完全移走字标。改用 300 DIP 视口，并先严格断言完整包络处于 clip 顶边外；产品 cull 未改。实际坐标见下方证据。
3. 第二轮 headless ExitCode=1、4 项失败来自强化后的触屏/笔 fixture：ImGui 会去重相同位置的 MousePos，AddMouseSourceEvent 只设置下一事件来源，不能单独改变 io.MouseSource。修复为真实 2 DIP 位移并明确断言真实来源；严格非零鼠标倾斜与归零断言保留，未修改产品/backend 或直接写 io.MouseSource。
4. 最终编译及完整测试通过，所有上述断言继续启用。

## 同机 WARP 像素与性能证据

Windows 11 ARM64 / Debug，同一 1000×1100 RGBA8 目标、生产 RenderHome、真实字体/DrawData、现有 imgui_impl_dx11 和产品预编译 CSO，D3D11CreateDevice 明确请求 WARP FL11.0；不创建 HWND/swap chain。每模式 20 帧预热、100 帧采样。下表单位 ms；CPU 为构建真实主页 DrawData，完成提交包含 D3D 提交、执行及 CopyResource/Map 等待和读回，不称 GPU-only。

| 模式 | CPU median / p95 | 完成提交及读回 median / p95 | 动效缓存重算次数 |
| --- | --- | --- | --- |
| static | 1.598 / 2.062 | 9.164 / 9.847 | 0 |
| idle | 1.556 / 1.986 | 8.209 / 8.758 | 0 |
| moving | 2.187 / 2.778 | 10.249 / 11.136 | 100 |
| returning | 2.297 / 2.802 | 10.816 / 15.323 | 200 |
| entrance | 4.093 / 4.637 | 8.485 / 11.175 | 100 |

所有模式均为 14166 顶点 / 59346 索引 / 6 批次；100 个采样帧内 ImGui 分配计数和品牌缓存扩容均为 0，idle 重算为 0。returning 模式在计时外周期性先建立鼠标偏转，因此缓存重算计数包含这些准备帧。上述计数不覆盖页面原有 std::string/vector 或驱动分配，不能扩大为整页零堆分配；Debug 数值仅作同机对照，不代表 Win7 设备性能保证。

冷启动真实第一帧裁剪、后续可见推进、透明笔画/下伸、浅深反光/四角视差及实际离屏坐标：

```text
[HomeWARP] cold-entry clipped=1 visible=11 entries=1 elapsed=0.183333
[HomeWARP] stable-theme=light frames=21 identical-readbacks=20
[HomeWARP] transparent-ink solid=23190 AA=2406 lower=1313 outside=0
[HomeWARP] pixels mode=shine changed=13177 outside=0
[HomeWARP] pixels mode=parallax changed=25203 outside=0
[HomeWARP] pixels mode=parallax changed=26278 outside=0
[HomeWARP] pixels mode=parallax changed=26243 outside=0
[HomeWARP] pixels mode=parallax changed=25680 outside=0
[HomeWARP] stable-theme=dark frames=23 identical-readbacks=3
[HomeWARP] transparent-ink solid=23190 AA=2406 lower=1313 outside=0
[HomeWARP] pixels mode=shine changed=13161 outside=0
[HomeWARP] pixels mode=parallax changed=22584 outside=0
[HomeWARP] pixels mode=parallax changed=23688 outside=0
[HomeWARP] pixels mode=parallax changed=23630 outside=0
[HomeWARP] pixels mode=parallax changed=23092 outside=0
[HomeBrandDesign] scale=1 frames=2032 linkTaps=144
[HomeMotionCull] scale=1 viewport=800 scroll=331 max=331 brandBottom=52.2265 clipTop=16
[HomeMotionCull] scale=1 viewport=300 scroll=889 max=889 brandBottom=-505.774 clipTop=16
[HomeMotion] scale=1 entries=3 cacheBuilds=1237 allocations=1
[HomeBrandDesign] scale=1.25 frames=480 linkTaps=0
[HomeBrandDesign] scale=1.5 frames=480 linkTaps=0
[HomeBrandDesign] scale=2 frames=480 linkTaps=0
[HomeBrandDesign] scale=1.875 frames=480 linkTaps=0
[HomeMotionCull] scale=1.875 viewport=1500 scroll=617 max=617 brandBottom=101.55 clipTop=30
[HomeMotionCull] scale=1.875 viewport=562.5 scroll=1200 max=1657 brandBottom=-481.45 clipTop=30
[HomeMotion] scale=1.875 entries=3 cacheBuilds=1237 allocations=1
[HomeBrandDesign] scale=3 frames=480 linkTaps=0
[HomeBrandDesign] scale=4 frames=480 linkTaps=0
```

## 兼容、格式与范围

- 兼容基线仍为 Win7 SP1 + KB2670838 / WARP FL11.0；本次没有新增 FL11.1 专属产品依赖。源码兼容审计与 Windows 11 WARP 测试分开记录，Win7 实机没有运行；Microsoft 边界：https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7 。
- 已更新 rendering-and-ui.md 首页合同，以及 configuration-i18n-and-assets.md 有意空连接符规则；独立 reviewer 复核一致。
- UTF-8 严格解码、原 BOM/CRLF/末尾换行与 HEAD checkout 对照；新增文本 CRLF；git diff --check 通过。最终审计明细 `Inkeys/Cache/home-design-20261010/encoding-audit-final.json`。
- 未启动应用 GUI、设置窗口或浏览器，未用 Computer Use，未 commit/push。原生窗口最终观感、Win7 实机和实机输入保留人工验收；任务保持 in_progress 供用户审阅，不自动归档。

## 可复查本地证据

- `Inkeys/Cache/home-design-20261010/motion-build-verified-result.json` / `.log`
- `Inkeys/Cache/home-design-20261010/motion-headless-verified-result.json` / `.stdout` / `.stderr`
- `Inkeys/Cache/home-design-20261010/i18n-empty-tests.ps1` / `i18n-empty-tests.log`
- `InkeysHeadlessTests/setting_home_warp_tests.cpp` 与 `setting_design_tests.cpp` 中实际生产绘制/输入回归
