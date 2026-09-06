# Research: Setting 首批无窗口验证入口

- Query: 如何用现有测试工程验证新导航/卡片布局及真实 ImGui 字体，而不创建 HWND 或另建渲染框架？
- Scope: internal；接口与项目静态研究，未构建/运行。
- Date: 2026-09-06

## Findings

### 现有工程与入口

| 文件 | 直接确认 |
| --- | --- |
| `InkeysRepo.sln:13` | 已包含 InkeysHeadlessTests，无需另建 Solution |
| `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:59` | VcpkgEnabled=false；输出 `Build/$(Platform)/$(Configuration)/InkeysHeadlessTests.exe` |
| `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:65–83` | C++20、v143、/utf-8、Debug静态CRT、Console；现阶段没有 ImGui/ImFluent 编译项 |
| `InkeysHeadlessTests/animation_tests.cpp:871` | 唯一 main；并非 main.cpp。简单 failureCount 汇总，无 gtest/Catch/doctest |
| `InkeysHeadlessTests/animation_tests.cpp:879–898` | `--no-window` 只跳过 RunWindowTests，其余含 RunSettingSessionStateTests 仍执行 |
| `InkeysHeadlessTests/setting_session_state_tests.cpp:1` | 引用 Setting.SessionState.h/Layout.h/Theme.h；局部 Expect 返回 bool，失败计数后统一返回 |
| `Inkeys/Inkeys/UI/Setting/Setting.Layout.h` | 已有尺度、导航断点、page/titlebar geometry 纯 helper |
| `Inkeys/additional/imgui/imgui.h:1` | Dear ImGui 1.92.7 |
| `Inkeys/additional/imfluent/UPSTREAM.md:1` | ImFluent fe7cf3ef784afc81aed55f24f39fcaa15cdf96cb，MIT；现有嵌入字体/ResetContext patch |

新增测试应继续纳入原 executable，避免添加第三方测试依赖或一个必须启动窗口的 screenshot 应用。

### 推荐最小分工

- **生产实现代理**：新 Design/Typography 公共 helper，及真实控件/文字参数加载接口；helper 必须由产品使用，不能只是供测试写一套相同公式。
- **settings_tests 代理**：`InkeysHeadlessTests/setting_design_tests.cpp`（建议新文件）、`InkeysHeadlessTests/animation_tests.cpp` 的 Run 声明/汇总、`InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` 的源/头/必要 include 登记；若适合也可扩展原 `setting_session_state_tests.cpp`。
- **主会话**：统一完整 Solution build 和 test，避免多个代理同时构建锁定 module/output；审核完整修改和编码/换行。

具体新增生产 header 名称以实现代理给出的接口为准。研究代理不拥有上述文件；只提供建议。

### 值得做的边界检查

1. 导航状态序列：宽窗用户收起 → 自动窄窗 → 返回宽窗，用户偏好恢复；宽/紧凑/overlay 阈值使用 logical DIP。选择页与底部动作在 overlay 关闭 pane，定向插件入口不被默认子页覆盖。后两项若界面回调不可直接测则静态审查，不复制假的路由系统。
2. 行几何：不同 action 宽度都满足右边缘相同；有限、非负、不与 label 重叠。长中英说明换行增加文本高度；窄预算下 action 移至下一行、仍在卡片内。覆盖 960×700、720×520 与最窄实际内容预算，及有效尺度 1、1.25、1.5、2。
3. 字体：用仓库真 SC/TC regular/bold 和 icon face，验证中文、`Agpqy` 与所有 nav icon 非 fallback；检查 ink bounds 与行高/控件盒关系。字号/font scale 与 DPR 只应用一次。
4. DrawData：若已有公共绘制接口容易接入，生成一个真实 Button/Toggle/Combo/Slider/设置行，断言 draw data Valid、vertex/clip 坐标有限、action/text 子区域不重叠、当前窗口可見 glyph 不被行底截断。不要仅断言返回固定常量。

只测几何公式不能证明文字实际不裁切；只有 DrawData 也不能宣称已验证 DX11 呈现、DWM 或字体视觉“完全正确”。这两类结论必须区分。

### 无 HWND ImGui / ImFluent 的现有 API

纯 helper 检查无需新增链接项。如增加真实 CPU glyph/DrawData 检查，编译以下已随库文件即可，**不添加 Win32/DX11 backend**：

- `../Inkeys/additional/imgui/imgui.cpp`
- `../Inkeys/additional/imgui/imgui_draw.cpp`
- `../Inkeys/additional/imgui/imgui_widgets.cpp`
- `../Inkeys/additional/imgui/imgui_tables.cpp`
- `../Inkeys/additional/imfluent/imfluent.cpp`

include 搜索路径加 `../Inkeys/additional/imgui`，因为 imfluent 内部使用 `#include "imgui.h"`。ARM64 可沿产品的 `IMGUI_DISABLE_SSE` 定义。保留测试工程既有标准 Windows 链接；无新增网络包。若测试仅字体而不用 Fluent 控件，可省去 imfluent.cpp，但这不能验证其字体绑定。

建议 context 生命周期（无 GPU 的 CPU 测试适配）：

1. `IMGUI_CHECKVERSION()`、`ImGui::CreateContext()`；`io.IniFilename=nullptr`、`io.LogFilename=nullptr`，指定 `io.DisplaySize` / `io.DeltaTime=1/60`，不要调用任何 platform backend。
2. 为覆盖产品 1.92 动态多字号路径，设置 `io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures`；把 `platform_io.Renderer_TextureMaxWidth/Height` 设成 16384，匹配现有 DX11 backend (`imgui_impl_dx11.cpp:592`)。使用 CPU atlas 来表示纹理请求，测试不创 GPU 纹理。
3. 加载项目字体真文件、采用生产字体校准配置。`ImFluent::SetFluentTextStyleFont(style, font, nominalSize)` 已公开 (`imfluent.h:367`)，`SetThemePreset(Light)`、`PushFluentStyle/PopFluentStyle`、`PushFont/PopFont` 也公开。禁止 `LoadFluentSystemFonts()`，它会绕开项目字体。
4. `ImGui::NewFrame()` → 固定位置/大小测试 window → 实际公共控件 → End → `ImGui::Render()` → `GetDrawData()`。必要时画第二帧以稳定自动高 child，但不要用恢复/忽略 assert 隐藏不配对的栈。
5. `ImGui::GetFontBaked()` / `font->GetFontBaked(size)`；`FindGlyphNoFallback(codepoint)` 与 glyph `X0/Y0/X1/Y1` / `AdvanceX` 检查字面和可见框 (`imgui.h:527,3597,3851,3903`)。普通 FindGlyph 会返回替代字形，不能用于验证覆盖率。
6. CPU DrawData 检查不需真实上传纹理。若访问 `ImDrawCmd::GetTexID()`，应先模拟处理 `drawData->Textures` 的 WantCreate/WantUpdates：读取合法 CPU pixels，`tex->SetTexID(nonzeroToken)`、`tex->SetStatus(OK)`，WantDestroy 用 Invalid/Destroyed；不要直接改 Status 字段 (`imgui.h:3485,3543,3934`)。否则 GetTexID 会因未处理 atlas 请求 assert。不要把模拟 ID 传入真正 renderer。
7. `ImFluent::ResetContext()` 在 `ImGui::DestroyContext()` 前清理其 process-global 状态；不要让上一轮字体指针残留到下一个 fixture。

旧 `Fonts->Build()`/SetTexID 路径仍存在，但不声明 RendererHasTextures 时不同字号走兼容缩放，因此不宜用其截图对产品动态字体下结论。校准参数应由生产共享接口提供，测试不独立硬编码一份比产品更“正确”的字体配置。

### 字体实际资源

`Inkeys/Inkeys.rc:180–194` / `Inkeys/resource.h:14,45–49,71–72`：

| ID | 文件 |
| --- | --- |
| 198 | `Inkeys/src/ttf/HarmonyOS_Sans_SC_Regular.ttf` |
| 258 | `Inkeys/src/ttf/HarmonyOS_Sans_TC_Regular.ttf` |
| 297 | `Inkeys/src/ttf/HarmonyOS_SansSC_Bold.ttf` |
| 298 | `Inkeys/src/ttf/HarmonyOS_SansTC_Bold.ttf` |
| 257 | `Inkeys/src/ttf/Segoe Fluent Icons.ttf` |
| 262 | `Inkeys/src/ttf/icomoon.ttf` |

测试 executable 当前没有产品 TTF resource，因此不能原封不动调用 GetModuleHandle(NULL)/FindResource 的产品加载器。最小方式是从仓库路径读取同一 TTF 字节后调用共享配置/组装 helper；路径基于明确 repo root 或 exe-relative Build/ARM64/Debug 回溯，并对文件缺失报错，不默默 fallback 默认字体。

## Build / Run Commands

由主会话使用本机已定位的 **ARM64 host MSBuild.exe**，超时至少 5 分钟：

```powershell
& '<Visual Studio ARM64 host MSBuild.exe>' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m
& '.\Build\ARM64\Debug\InkeysHeadlessTests.exe' --no-window
```

不单独编译 Inkeys.vcxproj。Headless exe 是 Console，应通过当前工具无窗口运行，不用可见 Start-Process。不运行 `RunWindowTests` 或启动产品。实际 MSBuild 绝对路径以主会话查得的本机位置为准，本研究没有定位/执行构建。

## External References

- Vendored ImGui 1.92.7 API/comments：`Inkeys/additional/imgui/imgui.h`；上游 `https://github.com/ocornut/imgui`。
- Vendored ImFluent commit/本地扩展记录：`Inkeys/additional/imfluent/UPSTREAM.md`；上游 `https://github.com/lukaasm/ImFluent`。
- 此研究依赖仓库当前固定版本，未将未验证的在线最新 API 当成本地接口。

## Related Specs

- `.trellis/spec/native-desktop/build-and-compatibility.md`：完整 Solution Debug|ARM64、ARM64 host、至少 5 分钟。
- `.trellis/spec/native-desktop/rendering-and-ui.md:790` 起：Setting resident/shared pipeline、字体/DPI串行所有权及 `--no-window`。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`：字体资源和 generated i18n 边界。
- `research/approved-first-batch.md`：小型无 HWND 检查随页面实施，不建大型离屏引擎。

## Caveats / Not Found

- 现有测试尚未链接 ImGui/ImFluent，以上真实 glyph/DrawData 路径是可用 API 组成的建议，未执行证明编译通过。
- `--no-window` 的现有总输出为 `PASS animation correctness`，并不是仅执行动画；失败统一 count。不要把这个固定字符串当成新页视觉已通过。
- 全部研究只读产品和测试；未启动窗口、D3D renderer、Shell 动作或实际测试。