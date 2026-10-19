# Cursor implementation boundary (2026-10-19)

本子任务只补足当前 resolver/瞬态 renderer 与产品光标偏好的差距。输入样本仍是热点，笔形的中心偏移仅作用于 visual/renderer/脏区；不修改输入位置或 L0/L1/L2 内容。修改范围限 PenCursor、DrawingController、RendererPrimitives、正式 VS/PS、Win7 guarded PS 和既有 contact headless 测试。WindowControl、产品/配置/设置由主会话及另一实施者负责，不在本子任务改动。

- 保留旧公共光标 API 的默认语义；Host 首次 SetCursorSettings 后才启用产品按场景外观。
- Mouse Pen/HardPen 使用 24 DIP 的右下斜笔形，左上尖端为原 sample 热点；Highlighter 保留实际宽度矩形。
- 新 appearance 字段覆盖中心偏移和独立边缘 opacity；CPU 等价/边界、renderer 上传和 VS/PS 同步。使用未占用 shape 21。
- IntegratedPen 使用 ScreenPen；ExternalPen/Unknown/Conflict 使用 Digitizer 的外观偏好，Mouse 使用 Mouse；不写回 source 身份。
- 半透明填充沿原 appearance；高亮只加强边缘，Highlighter 填充仍为 0.35。Eraser/Laser、Pen Contact 隐藏、倒转笔尾 alpha 保持既有合同。
- Shader 兼容变体沿 archived 生成方法再生；当前任务生成器将 DefaultInk 正向门上界同步至 21，使新增 PenIcon 同样被排除。OS/driver 选择不变。
- 主会话统一执行完整 solution/headless；本子任务不构建、不运行产品/测试、不创建 HWND。

## 实现结果

- `DrawingController::SetCursorSettings` 以单个原子快照发布四项偏好和显式启用位。每次调用均同步 Window 的系统鼠标策略；首次接入或静止边缘变化也请求重建瞬态光标。旧公共 API 和未接入产品偏好的默认 resolver 路径保持兼容。
- Resolver 增加可选产品设置、Pen/HardPen 图案作用域和 DPI 参数。Mouse 的 PenIcon 为 24 DIP，五边形笔身从左上尖端延伸到右下；输入 x/y 不动，centerOffset 为半图标尺寸。Highlighter 保持现有实际 canvas 宽度和 0.35 填充。产品场景高亮不受旧 translucentInkCursorEnabled 覆盖。
- Mouse/IntegratedPen/ExternalPen 分别使用 Mouse/ScreenPen/Digitizer。Conflict 优先于已知 kind 回退 Digitizer，Unknown 同样回退；不会改写 source 或橡皮响应。Pen Contact 的独立显隐及倒转笔尾保持原有行为。
- `outlineOpacity=-1` 兼容继承整体 opacity；新显式值只影响边缘。`InkPoint[1].r` 编码 outlineOpacity+1，经 VS 原有 UV 传到 PS；旧上传值 0 仍保留原公式，buffer/CB/寄存器/PS 输入签名均未扩展。高亮边缘为 1.25 DIP、0.96 RGB、opacity 1；半透明边缘为 0.5 DIP、184/255 RGB、工具有效填充 alpha。实际墨迹的工具 alpha 不变。
- offset/outlineOpacity 已进入 appearance 校验、视觉等价和 CPU bounds；renderer 按同一 offset 生成 quad。旧/新 dirty 并集仍走已有 Controller 路径，光标只叠在 backbuffer，未进入 L0/L1/L2 或导出/撤销。
- 新增生产 resolver headless 用例：鼠标 pen/highlighter 悬停和接触、系统样式、DPI/负坐标裁剪、三场景交错与 Conflict/Unknown、旧透明度 API 隔离、Pen Contact、倒转橡皮、Touch primary 禁止、offset/edge 等价和非法 offset。

## Shader 生成与静态核验

使用当前任务 `research/Generate-Win7-GuardedShader.ps1` 生成。该文件从归档生成器复制，唯一差异是 DefaultInk gate 从 `type > 20` 改为 `type > 21`；当前任务目录的原四级上溯可以直接定位仓库根，不再使用 ignored scratch 内的旧生成器。输出位于 `Inkeys/Cache/CursorPolicyShader/generated/`，包括无 BOM ASCII 参考源、正式参考 VS/PS、guarded CSO/ASM 和完整 `guarded-source-evidence.json`；实际产品资源仍由 vcxproj 的唯一 FXC 构建链生成。

- 实际工具：`C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/arm64/fxc.exe`，动态发现 ARM64 FXC；VS `vs_5_0 /O3`、PS `ps_5_0 /O3` 和完整 guarded PS 编译退出 0。
- 正式 PS 源 SHA256：`7EA2564A3A5FCD244DA2124095FA0B262DBA590629C0A5ECC228F758936FF977`。
- 正式参考 PS CSO SHA256：`C91DDD5E913F30AE7056D5D4504270EEF9062D789C339ED2182B88926E79CAEA`。
- 新 guarded PS：31160 bytes，SHA256 `43C7E21FE9AA531271E2036D44998A5DCE6EAC9C032B63965A5417AE293C8D57`；已写回 `Draw3.Win7GuardedShader.h`，保留 UTF-8 BOM/CRLF。
- ISGN `4493E3D3258BE71AEC723F26979FE3A407C4ECCFAE277B41BFD046DAC55B19E6`，OSGN `F9355CCAD20A6284685169A043421A10B2BDC8395B02145240D94A39524F3128`，正式参考/guarded 相同。
- 当前 guard 为 type 0/3/<0/>21，明确排除新增 type 21。审查指出 Win7 WARP 历史缺陷本身就与未执行 DefaultInk 的控制流有关，不能仅凭前部 return 断言安全；初次生成的 >20 版本已被替换。新增图元与已有提前 return 分支同样从 DefaultInk 正向门排除，未知 >21/<0 及真实 DefaultInk 0/3 保留原数学。还原尾部 SHA 与原尾部均为 `84B1C4CF3F07A25ACBB5B9C081A91B2269AA62C47B67A0AA83BFB049BD2096A5`，`math_preserved`/`unknown_type_preserved` 为 true；25 条 derivative/discard 指令全部位于正向 guard 内（depth≥1）。
- 没有改 OS/backend/diagnostic shader 选择。现有 raw cursor GPU probe 的第二 InkPoint.r=0 仍表示继承 opacity，上传合同保持向后兼容。
- `git diff --ignore-submodules=all --check` 通过；各源码保留原 BOM 和 CRLF，contact tests 保留无 BOM + CRLF；只新增必要改动。

## 验证边界

本子任务只运行 shader 工具和静态检查，没有编译或执行产品/测试，没有创建 HWND/GUI。主会话负责统一完整 solution Debug|ARM64 和 headless --no-window。新 shader 的目标 Win7 WARP、实际 DPI/跨屏/亮暗背景以及首次接触效果均须现场验证；FXC 通过与签名相同不能证明 Win7 驱动实际像素等价或系统光标已经修复。
