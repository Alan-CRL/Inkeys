# Startup Preview 验证记录

## 2026-09-04 本轮结论

程序化灰色占位实现已完成自动验证。此前 BIN、494x105 抓帧、CRC/signature、Gaussian blur、proxy/staging、capture 和三类 cache 交接的历史 PASS 均已废弃，不作为本轮证据。

## 构建证据

使用 x64 `MSBuild.exe` 构建完整 `InkeysRepo.sln`，并将 `TEMP/TMP` 指向仓库内临时目录以适配沙箱。下列命令均退出码 0：

```text
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug   /p:Platform=x64  /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Release /p:Platform=x64  /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug   /p:Platform=Win32 /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Release /p:Platform=Win32 /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug   /p:Platform=ARM64 /m:1 /nr:false
MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Release /p:Platform=ARM64 /m:1 /nr:false
```

构建仍输出仓库既有的第三方 `hashlib++`、`fmt`、Win32 SDK module 和窄化转换警告；本任务未新增编译错误。

## Headless 与 smoke

同样将 `TEMP/TMP` 指向仓库内临时目录后，以下四个本机可执行目标均输出 `PASS animation correctness`，退出码 0：

```text
Build/x64/Debug/InkeysHeadlessTests.exe --no-window
Build/x64/Release/InkeysHeadlessTests.exe --no-window
Build/Win32/Debug/InkeysHeadlessTests.exe --no-window
Build/Win32/Release/InkeysHeadlessTests.exe --no-window
```

ARM64 Debug/Release headless 均成功构建，但当前 Windows x64 宿主返回“not a valid application for this OS platform”，因此运行标记为 **NOT RUN（宿主架构不支持）**，不能写 PASS。

隐藏运行 `Build/x64/Debug/Inkeys.exe --startup-preview-smoke <report>` 实际得到：

```text
PASS
totalWidthDip=470.000000
previewFirstAlpha0Committed=1
previewFadeOutCommitted=1
barAlpha0Committed=1
barAlpha255Committed=1
automaticStopPosted=1
ownerThreadExited=1
previewInactive=1
recovery=visible
requestedAlpha=255
committedAlpha=255
```

报告不再包含 `cacheState`、signature、capture、proxy 或 deblur 字段。

## 逻辑与静态覆盖

- Headless 断言覆盖默认 `380/470 DIP` 推导、折叠 80 DIP 缝隙、非法缓存回退、DPI x zoom 舍入和有限值去重写回。
- Headless 通过生产 `Config::ReadMini` 读取临时旧版 `main.json`：已知 Enable/zoom 正常读取，未知旧字段被忽略，缺失 `CachedStartupBarWidthDip` 保持 470 DIP schema 默认值。
- Headless 断言覆盖 alpha-0 首帧、单调 fade、Preview committed 0 后才启动 Bar fade、Bar committed 255 后才销毁，以及 bypass 恢复门。
- Headless 断言覆盖 shimmer 本地 epoch、cosine ease、不同尺寸下完整 soft-tail 两端离屏和 wrap 的 base-only 端点。
- Headless 断言覆盖 2999ms/3s/180ms 进度门、真实 milestone 上限、无 300ms hold、自动重试普通颜色和最终 fatal 红色。
- 源码静态检查确认每次 Preview ULW 都填写 `pptDst`、`psize`、`pptSrc`，blend 为 `AC_SRC_OVER/0/AC_SRC_ALPHA`；`FillOpacityMask` 前后切换并恢复 antialias mode。
- Startup Preview 专属源码/工程登记中不再出现 BIN、Format/VisualConfig/CacheWrite、image cache、signature/layout epoch、Gaussian blur、live proxy、CPU staging 或 `--capture-startup-preview`。Bar 其他视觉效果中既有 Gaussian 保留，未误删。
- RC/resource/project/filter 无悬挂图片资源，PptCOM activation manifest 资源 221 保持原编号；项目依赖没有新增高版本静态 import，Shcore 仍为动态加载。
- 本任务修改的源码、工程和任务文件已恢复原 CRLF；`git diff --check` 无空白错误（仅提示两项既有无关文件仍为 LF），Trellis task validate 通过。

## 人工/平台验证（NOT RUN）

以下仍需专用环境或交互，本轮没有声称 PASS：

- Windows 7 SP1 + KB2670838、WARP、FL11.0、动态 Shcore 缺失回退；
- 96/120/144/192 DPI、混合 DPI、多显示器、负坐标和拓扑变化的实际视觉；
- 深浅桌面上的颜色/质感、shimmer 循环边界像素级截图比较；
- SuperTop 父/helper/UIAccess 完整链路；
- 点击吞噬、焦点、Alt-Tab、topmost 竞争和透明圆角 hit-test；
- 注入真实 device loss、GetDC/ULW/ReleaseDC/EndDraw failure；
- 首次自动重试与最终 fatal 对话框的人工视觉观察。
