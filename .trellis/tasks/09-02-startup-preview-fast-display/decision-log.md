# 决策日志：简化 Startup Preview

## 已确认的产品决策

| ID | 决策 | 原因 |
| --- | --- | --- |
| D-01 | T0 位于最终进程越过 SuperTop 所有退出/替换分支之后 | 父进程和 helper 不能闪现 Preview 或重复统计启动 |
| D-02 | Preview 是单一程序化中性灰圆角矩形 | 启动反馈不需要复制真实 Bar 内容，避免图片资源和错配 |
| D-03 | 默认完整外包络宽度为 `470.0 DIP` | `80 主按钮 + 10 间隔 + 380 MainBar 主体`，与当前默认按钮列规则一致 |
| D-04 | 唯一缓存是 `CachedStartupBarWidthDip` | 只缓存逻辑总宽度，避免图片/主题/语言/签名兼容矩阵 |
| D-05 | 缓存值非法时回退 470，不能取消快速显示 | 坏配置不应阻塞启动或使占位消失 |
| D-06 | 保留 D2D1.1 + ULW per-pixel alpha 与 `SourceConstantAlpha` | 需要圆角抗锯齿、shimmer、进度和整窗 alpha，同时保持 Win7 基线 |
| D-07 | 每次 ULW 显式填写 `pptDst`、`psize`、`pptSrc` | 该项目已验证省略 geometry 会出现“返回成功但不可见/不刷新” |
| D-08 | 静态形状 mask + `FillOpacityMask` 多段 soft-tail shimmer | 反光只作用于真实 alpha 区域，并避免循环端点跳闪 |
| D-09 | shimmer 相位来自首次显示的本地 `steady_clock` epoch | 不受 render frame timestamp epoch/取模跳变影响 |
| D-10 | 交接统一为 Preview committed 0 后 Bar committed 0→255 | 禁止两个 layered HWND 长时间中间 alpha cross-fade、透明停顿或图片分支错配 |
| D-11 | Bar 首个完整 committed frame 是 100% 与宽度回写门 | setter、queue 或部分 ULW 成功都不代表屏幕可见 |
| D-12 | 宽度写回不在 render thread | 配置/文件 I/O 不得阻塞共享渲染线程；只发布有限、去重 double |
| D-13 | 首次自动重试先正常颜色渐隐；最终 fatal 才红色 | 重试不是最终失败，用户不应看到短暂错误状态 |
| D-14 | 保留人工延迟、重试注入和 smoke 钩子 | 这些是观察启动顺序和失败恢复的必要测试入口 |

## 明确废弃的旧决策

- 不再使用 `StartupBarPreview-v1.bin`、embedded BIN、磁盘图片 cache、160-byte header、CRC-32、visual signature、layout epoch 或 `CacheState` 分类。
- 不再使用 Gaussian blur、embedded scale/padding、live Bar proxy、crop/CPU staging、stable-for-cache debounce、cache writer 或 developer capture。
- 不再保留 `--capture-startup-preview`、资源登记、capture validator 或与图片方案绑定的 smoke 字段。
- 不把 `BarMainBarWidthDip=80` 当作展开主体宽度，不把旧 `494×105` 抓帧尺寸当作默认逻辑尺寸。

## 实现默认值

- 灰色使用 `#808080`，形状 alpha 为 `0.74`；1 DIP 内描边 alpha 为 `0.16`，并向内绘制。
- Bar 高度/圆角为 `80/8 DIP`；进度条仍为 192 DIP 双层 Fluent 风格，满 3 秒约 180ms 渐显。
- Preview nominal 进度单位保持 1000；原 20 units 改给 `PreviewGeometryReady`，禁用时从 plan 移除。
- 所有等待、failure-frame 等待和 owner shutdown 都有界；Preview 失败只 bypass/recover，正式 Bar 最终尽力以 committed 255 可见。

## 实施结果

- 纯几何合同落在双方均可安全依赖的 `Inkeys.UI.Bar.Metrics` 与 `Inkeys.UI.StartupPreview.State` 中；`static_assert` 和 headless 同时锁定 70/75/380/470 DIP 推导。
- shimmer 行程使用 `ResolveShimmerHorizontalTravel` 按 mask 投影和完整 support 求端点，实际多段 stops 的首尾为零；不同 DPI/zoom 尺寸的两端离屏与 wrap base-only 断言已通过。
- 六套 Solution 配置构建、x64/Win32 headless 与隐藏 startup smoke 已通过；完整命令和输出记录在 `validation.md`。

## 仍需人工验证

Win7/WARP、混合 DPI/多显示器、深浅桌面视觉、输入/topmost、真实 device-loss/ULW 注入以及重试/fatal 对话框观察仍为 NOT RUN；不得由自动逻辑测试替代。
