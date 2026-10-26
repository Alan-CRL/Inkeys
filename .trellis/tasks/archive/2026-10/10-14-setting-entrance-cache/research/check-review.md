# 独立审查：设置开屏缓存

- Reviewer: trellis-check / entrance_check
- Date: 2026-10-14
- Context: prd/design/implement/check.jsonl、rendering-and-ui 设置及共享生命周期合同、cpp-conventions、cache-and-benchmark 研究；只读审查产品与测试，未运行构建、测试、GUI 或 Computer Use，未修改产品文件。

## Findings (fixed)

- File: `InkeysHeadlessTests/setting_home_warp_tests.cpp`
  - Issue: 初版 optimized 基准一律预先捕获截图，使等待/书写样本执行产品当时尚未使用的截图 copy。
  - Fix: 已反馈并由实施方修复；各 Benchmark 先 Reset，mode 4/8 仅预热 ready，entryElapsed=0，不预先 revealReady。真实 hold/dock/settle 才 capture/replay。已核最新代码。
- File: `InkeysHeadlessTests/setting_home_warp_tests.cpp`
  - Issue: 初版硬件设备创建失败会计入必需 WARP 回归失败；缺捕获/恢复分段和诊断范围标识。
  - Fix: 实施方已将可选 hardware D3D11CreateDevice 失败记录为 unavailable；加入 prepare-warm-fonts/capture/restore，输出 adapter/vendor/device/FL/尺寸、scale/theme/build，并说明吞吐范围。

## Findings (not fixed)

没有未修复的确定产品缺陷。主会话已定向同步 rendering-and-ui：单份 copy-only 截图、WritingEnd 捕获、冻结/恢复、material/backdrop 键、生命周期及 EVENT 测量合同；审查已核同步 diff。全套无窗口测试退出 0。

验证边界：当前像素夹具为 1000×1100、scale=1，实际 Home/Navigation/foreground 和生产 DX11 backend/CSO；标题栏为简化填充，不含原生 caption/business 图片、其他共享客户端、swap chain/Present。没有本轮真实设备移除、跨设备异常 copy、Win7 实机或 GUI 验证。27 个值键改变用 CPU 状态机断言；不能表述为 27 个真实设备重建测试。prepare-warm-fonts 样本已预热 atlas，不能表述为首次冷启动字体耗时。

## 实际代码路径审查

- `BeginHomeEntryFrame` 在 WritingEnd 门禁保持完整字标/不透明遮罩，两次成功 full frame 后才允许 capture。捕获时活动 `RenderHome` 保留字标占位而不提交字标，`SelectLists` 排除整个 foreground，因此截图不含字标/遮罩。
- 缓存只保留独立 DEFAULT texture 和值键。GPU CopyResource/CopySubresourceRegion 原样复制最终 RGBA，不做 AddImage 再混合。回写 BOX 按 framebuffer scale/DisplayPos 转换并 clamp，根 draw clip 截到标题栏；标题栏和 foreground 仍实时提交。
- 复用帧不生成正文/导航。终态前 restore 的两个 full-layout 成功帧仍由缓存遮挡；之后恢复原静态页面并 Reset。真实 Tab 取消仍阻断当帧动作，取消首帧 clip/hero 与后续焦点回归已随全套通过。
- backdrop 与最终 clear 使用同帧快照，material key 含 style alpha 与 WindowBg RGBA。尺寸/DPI/字体/语言/主题/atlas/epoch/key 变化先 Validate/准备；Hide、resize、font/device rebuild、资源失败、Present 非 S_OK、停止/终态均有同步 Reset，Hide 不依赖协程继续 Resume。临时 backbuffer 引用只在 Render 内存在。
- 捕获/overlay backend 任一 HRESULT 失败会 Reset，生产不 Present 残缺帧，沿原 Retry/DeviceLost 仲裁。ready 要有当前 frame 成功、字体事务 ready 和 captured；失败回滚动画并受六次预算限制。
- EVENT+Flush+GetData 等完成，10 秒有界；benchmark 的 pixels=null 路径没有 staging Copy/Map、Sleep、Present 或共享调度器。RGBA correctness 才走 staging readback。CPU/backend/wait/submit-complete/total 均为墙钟；total 逐样本直接测 begin→completed，未相加多个中位数冒充总中位数。硬件设备仅测试独立 device，没有改产品默认 WARP。
- `--setting-home-only` 分支位于 `--no-window` 的 SetErrorMode/CRT stderr 处理之后。正常产品仍 `Present(1,0)`，共享 deadline 仍 16,666,667ns；本次没有更改限帧策略。

## Verification

- Lint/format: pass。`git diff --check --ignore-submodules` pass；主会话 i18n check 两语言 384/384 pass，format-audit.json allValid=true（9 文件）。审查方另核六个现有源码 UTF-8 strict decode；animation_tests BOM 保留，其余无 BOM，工作文件均 CRLF，无 bare LF；新 helper 为 UTF-8 无 BOM/CRLF。Git HEAD blob 为规范化 LF，未据此误判工作文件 CRLF。
- TypeCheck/compile: 主会话完整 `InkeysRepo.sln Debug|ARM64`、ARM64 原生 MSBuild pass；`Inkeys/Cache/setting-entrance-cache-20261014/build.status.json` exitCode=0，87.8363113s。此审查方未重复构建。
- Tests: pass。主会话全套 `--no-window`，headless.status.json exitCode=0、304.7931987s，stdout 末行 `PASS animation correctness`，stderr 空；审查方独立读取了状态及日志。浅深、Solid/透明/部分 alpha、6 个缓存姿态、捕获失败/有限恢复、终态精确 RGBA、真实 Tab 取消与 27 值键状态回归均包含在通过的套件中。

## 实测证据边界与性能结论

- 同面积 Debug WARP dock 完整重绘 total median 39.9812ms（25.0118FPS），缓存 15.4169ms（64.8639FPS）；submit-complete median 37.6026→14.3562ms，CPU 2.5900→1.0175ms。单帧墙钟中位数直接从实际 total 样本获得。
- WARP 缓存热帧主要耗时在渲染完成阶段，剩余约 14ms；hardware Adreno X1-85 相同 dock 缓存 total 1.7974ms（556.359FPS）、CPU 1.0401ms、submit-complete 0.7146ms，软件/硬件路径分列。这支持当前 WARP 软件渲染执行/完成是主要瓶颈；没有字标消融或 timestamp busy 查询，不能把全部剩余耗时确定归给字标几何或 GPU shader。
- 捕获是一次性成本，WARP capture total median 46.8521ms；prepare-warm-fonts 与 restore 另列，不能将缓存稳定帧收益推广到准备帧。
- 正常产品仍有最多约 60FPS 共享调度节拍及 Present(1,0) 垂直同步。离屏每帧 EVENT 串行等完成测的是无限显示节拍限制下的完整单帧处理速率，不能当屏幕显示 FPS 或多帧在途管线峰值。
