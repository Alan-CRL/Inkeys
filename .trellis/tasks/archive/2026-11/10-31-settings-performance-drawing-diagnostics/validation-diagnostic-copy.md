# R10 诊断复制与名称验证

日期：2026-11-01。用户要求两个只读信息块的完整复制，以及画布/EDID技术名称明确化。

## 完成

- 画布与EDID块下各一个右对齐标准“复制”按钮，独立ID、标准自然宽度/32DIP高度；点击复制该帧显示的同一完整字符串，包含多行与全部显示器；按钮后恢复左侧cursor。
- DXGI_FORMAT、DXGI_SWAP_EFFECT、DXGI_ALPHA_MODE、DXGI_SCALING和D3D_FEATURE_LEVEL使用完整SDK名称及真实数值。
- Flags显示SDK全部13个已知flag的完整名称，组合/未知位/总hex保留，0明确无标志；SampleDesc为Count/Quality，1/0标明无MSAA；HRESULT为符号名+8位hex，未知保留数值。
- Hardware说明为硬件GPU；DirectComposition主模式保持；辅助ULW补UpdateLayeredWindow全称，输入后端补RealTimeStylus全称。RTS摘要中文状态并保留失败阶段，UMD补用户态图形驱动全称。
- DPI为X/Y和乘号，EDID全称只在块首一次，原解析状态及显示器数据保留。
- 纯格式helper复用Diagnostics模块，只改原owner的缓存文本；未改设备/输入选择、回退、查询时机或控制台输出格式。

## 最终检查

| 检查 | 结果 | 记录 |
| --- | --- | --- |
| 完整InkeysRepo.sln Debug / ARM64 | 退出码0，最终增量0错误 | validation/diagnostic-copy/build-arm64-final.* |
| --draw3-device-policy-test | 退出码0，格式与原设备策略均PASS | validation/diagnostic-copy/format.* |
| --setting-attention-only --no-window | 退出码0，包含真实复制按钮/ClipboardProbe | validation/diagnostic-copy/attention.* |
| --setting-copy-only --no-window | 退出码0，三语言文案与复制 | validation/diagnostic-copy/copy.* |
| 静态复核、BOM/CRLF、git diff --check | 通过，无源码阻塞 | research/diagnostic-copy-review.md及工具输出 |

复制测试使用fake clipboard hook：检查块下方、宽窄/主题/DPI、标准尺寸、右边缘、ID与后续cursor，以及中文长多行/四屏、首部离开viewport后的完整复制、当前帧更新数据。不写真实系统剪贴板。

格式probe不创建GPU/HWND：涵盖用户截图实际值、组合/未知flag、未知enum/HRESULT、SampleDesc、真实DXGI_SWAP_CHAIN_DESC1值组合、RTS就绪/失败摘要。所有常量以本机SDK26100 headers核对。

首轮源码编译发现DXGI_ALPHA_MODE_FORCE_DWORD（0xffffffff）向UINT aggregate隐式窄化C2397。仅将该项显式static_cast<UINT>，保留完整32位哨兵值后，同solution/config重建与全部专项通过；不是SDK/依赖问题，没有改工程或屏蔽检查。

## 验证边界

沿用既有ARM64原生MSBuild与同invocation PATH规范化，在正常Windows环境构建。未启动应用/设置GUI、Computer Use、hidden HWND fixture或实际设备故障注入；未重复无关HomeWARP基准。数据来源和owner边界有静态复核，纯值测试不能冒充实体驱动回退验收。

未创建commit、push或自动归档，前轮功能及未提交工作保留。
