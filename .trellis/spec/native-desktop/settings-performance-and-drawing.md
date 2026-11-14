# 性能设置与每笔绘图偏好

## 1. Scope / Trigger

修改性能页、通用绘图、激光设置、共享界面节拍、Draw3 启动帧率或画布初始化信息时应用。产品是当前 UI3 / Draw3，不能恢复传统 deploy.json 已退役的 Performance 容器。

## 2. Signatures

- `RenderPipeline::Scheduler::SetFrameRate(int)` 与进程级 `RenderPipeline::SetFrameRate(int) noexcept`：共享界面帧率。
- `Host::SetGeneralDrawingOptions(bool multiTouch, bool tailCorrection, int predictionMode) noexcept`：传统头不前置声明 module 所属枚举；Controller 内部接口使用 `InkPredictionMode`。
- `Host::SetLaserOptions(bool multiTouch, bool particles) noexcept`：多指偏好受 Host 的全局多指约束；粒子参数保留兼容，但由 Controller 强制关闭。
- `CreateStrokeModelConfiguration(int dpiX, StrokeTimingProfileId profile = Fps120)`：整套画布模型参数。
- `Host::CanvasInformation() const -> std::string`：复制 owner 已发布的 UTF-8 初始化文本，不在 UI 线程访问 COM。

## 3. Contracts

以下字段均位于 Inkeys/Config/main.json，新字段不上传：

| 路径 | 默认与合法值 | 生效边界 |
| --- | --- | --- |
| UI.FrameRate | 60；30/60 | 共享界面后续批次，设置/主栏/翻页同一节拍 |
| Drawing.FrameRate | 120；30/60/120/240 | 下次启动，完整 profile，不仅改等待时间 |
| Drawing.MultiTouchEnabled | true | 新独立 Touch 绘图准入 |
| Drawing.PenTailCorrectionEnabled | true | 新笔锁存，完成态拟合读取该笔值 |
| Drawing.InkPredictionMode | 2；0/1/2 | 新笔锁存，0=Disabled、1=StrokeEnd、2=Kalman |
| Drawing.Laser.MultiTouchEnabled | false | 后续新 Laser Touch，受全局多指约束 |
| Drawing.Laser.ParticlesEnabled | false，兼容字段 | 产品忽略启用值，构造与setter均强制关闭 |

- UI 下拉顺序是精准/快速/关闭，必须显式映射为 2/1/0。快速模式追至最近输入位置，不外推未来；普通说明描述效果，不暴露实现类名。
- 新笔锁存预测和尾修正，同笔 Move、Up、等待断触续接后固化均读旧值；成功接续保留旧 modeler 和锁存值，新独立笔才读新偏好。
- Disabled 不调用 Predict，但保留 Update、正常 ModelEndOfStroke、平滑和独立尾修正。普通笔、Shape、Laser、候选接续四处判断必须一致；橡皮保持本来禁用预测的规则。
- 关闭尾修正只跳过 FinalizeStoredStroke 的 FitCompletedPenGeometry，保留正常 taper、公切线、真实 Up、Stored 和历史提交。当前修正使用 ReconstructAdaptiveTerminalTail，旧阶段二“尚未接产品”是历史说明。
- 全局单指限制 Touch，不限制 Pen/Mouse，不截断已准入笔画。拒收触点走 DiscardUntilTerminal，保留 Up/Cancel；手势登记必须对称，不让被忽略触点中途接管。真正接入旧笔的接续不能被新增单指策略误拒。
- 激光多指有效值是全局多指与保存子偏好的合取；隐藏不清空子偏好。Host 在 render owner 应用，setter 幂等，不能每批重复 control wake 导致空转。
- 粒子条目不显示toggle，仅保留BETA禁用说明；Controller构造配置true、setter请求true也始终关闭，不因旧配置恢复，重复请求不产生额外wake。保留算法/Shader/配置字段，不扩大为资源清理任务。
- 绘制页先“绘图环境”（设备项）、后“通用”；移除统一新笔提示但保留每笔锁存。界面60FPS、画布120FPS、精准显示本地化推荐标记，数据值不变；侧栏性能与实验glyph对调，不改变路由。
- 原硬件加速、DComp、兆芯兼容三项仅迁移至性能页，继续使用 Experimental.Inkeys3.Draw3 下原键，均重启生效。
- 共享 Scheduler 保留原子请求、固定顺序、唯一 render owner、Idle/Continue/Retry 和设备恢复；帧率变更不制造永久渲染，也不修改动画倍率或输入采样。
- 画布设备、adapter、交换链和 RTS 信息在实际初始化/恢复/Resize 边界发布。初始化新代次清旧成功详情，停止后明确标为最后记录；Hardware 偏好不等于实际 Hardware，DComp 偏好不等于最终呈现模式。
- 初始化文本不依赖 ConsoleOutput、不解析日志、不轮询设备；只在 owner 查询 GetDesc/GetDesc1，设置 getter 短锁复制缓存。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 帧率/预测类型错误、超宽整数或非法档 | 回退该字段默认值；不窄化或把 bool 当整数 |
| 一笔中途修改预测/尾修正 | 旧笔及成功接续不变，下一独立笔采用新值 |
| 关闭全局多指且有旧笔等待续接 | 仍按既有身份、时间和运动判定真正续接；其他额外新指拒收 |
| 关闭预测后 Up | 不调用禁用 predictor；正常模型收尾与真实终点保留 |
| 尾拟合拒绝/关闭 | 保持合法完成基线，其他笔锋处理照常 |
| 关闭多指后再打开 | 激光子项恢复原保存值，不写成 false |
| 旧配置或调用方请求开启粒子 | 构造/setter/Get均保持关闭，UI无toggle，不增加wake |
| GPU/呈现/输入回退 | 信息显示实际 WARP/ULW/BasicXY/Mouse，与同代设备一致 |
| 查询失败或停止 | 显示不可用/错误或历史标记，不用模板冒充当前成功 |

## 5. Good / Base / Bad Cases

- Good：旧笔按精准起笔，中途改关闭，旧笔及其断触接续正常结束，下一笔无预测；关闭尾修正仍保留正常收笔。
- Base：缺项配置保持界面60、画布120、多指/修正开启、精准预测、激光两项关闭。
- Bad：仅在 Run 开头改 model params、Up 时读最新偏好、直接回收 producing touch、通过每帧 DXGI 查询填信息块。

## 6. Tests Required

- 配置真实磁盘 roundtrip、缺项、类型/整数宽度/白名单、激光子偏好保留。
- 真实Controller对象使用粒子配置true构造，再重复setter(true/false)，Get始终false且wake generation/pending不变；不Attach/Initialize/Run，不创建HWND。
- 真实 Scheduler 30/60、运行时切档、多客户端同批、idle/Stop。
- 真实模型/生产 probe 覆盖三预测模式、四帧率、尾修正开关、并发笔快照、候选接续、终态回收及池复用；不能只断言枚举或对末端 helper 手工赋值。
- 初始化文本发布/合并、新代清理、停止标记和真实 owner 回退路径；无窗口结果不能宣称实体 GPU、RTS 或桌面视觉验收。
- i18n sync/check、完整 InkeysRepo.sln Debug|ARM64 及适用无窗口测试；不自动启动 GUI。

## 7. Wrong vs Correct

Wrong：`ApplyPredictionMode(params, latestMode)` 仅调用一次，然后全局 constexpr 控制所有 Predict；Finalize 时读取配置开关。

Correct：新笔锁存 mode/correction，Reset、全部 Predict 判断及 Finalize 共用该笔快照；Host 仅发布期望值，不重置旧笔模型。

## 诊断文本与复制（R10）

### 1. Scope / Trigger

修改画布初始化/显示器EDID信息、SDK名称格式化或复制入口时应用；仅显示格式与复制，不改变设备、交换链、RTS/鼠标回退或查询线程。

### 2. Signatures

- `Design::CopyButton(const char* id,const char* label,const char* text) -> bool`：标准按钮尺寸、块下右对齐、点击后ImGui::SetClipboardText，恢复原左侧cursor。
- Diagnostics模块的 `FormatDiagnosticFeatureLevel/Format/SwapEffect/AlphaMode/Scaling/SwapChainFlags/SampleDesc/HResult/SwapChain/RtsAttempt`：纯值到字符串，无设备查询。
- `RunDiagnosticFormattingProductionTest()`：由既有 `--draw3-device-policy-test` 调用，无GPU/HWND。

### 3. Contracts

- 两块各一个独立ID的复制按钮，使用该帧已显示的完整canvasInformation/edidText，包含隐藏于滚动区域的多行/全部屏幕；不能重新查询再复制，不能只复制可见DrawData。
- 标准ButtonWidth（至少72 DIP自然宽度）、ControlHeight=32 DIP，在可用范围内右齐；复制后后续标题回到原左起点。
- 水平对齐使用成对的正值Indent/Unindent，offset=0不调用Indent（0会使用默认IndentSpacing）。不得在Button后用SetCursorPos/SetCursorScreenPos恢复到含尾部ItemSpacing的Y；该位置不在CursorMaxPos内，若作为最后item会在EndChild/End报错。保留Button的LastItemData、原间距，不改ImGui检查旗标。
- 已知枚举显示完整SDK常量：DXGI_FORMAT_*、DXGI_SWAP_EFFECT_*、DXGI_ALPHA_MODE_*、DXGI_SCALING_*及D3D_FEATURE_LEVEL_*，数值不丢。
- Flags逐位显示当前SDK的完整DXGI_SWAP_CHAIN_FLAG_*名称，组合用`|`，未知位和总32位hex保留；0为无标志，不能编造NONE常量。
- SampleDesc明确Count/Quality，1/0注明无MSAA；HRESULT显示已知符号名和8位hex，未知结果保留数值。
- Hardware说明硬件GPU，辅助ULW显示UpdateLayeredWindow全称，主DirectComposition保持。输入后端使用RealTimeStylus (RTS)，尝试摘要为中文就绪/失败并保留失败阶段；用户态图形驱动（UMD）版本名称完整。
- DPI标签为X/Y和乘号，EDID全称只在块首出现一次，原解析状态与真实显示器数据保持。
- 所有图形/输入文本仍在原owner的初始化/恢复/Resize边界缓存，UI只读取；控制台日志格式不因UI摘要改变。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 多行/多屏或首部离开viewport | 复制完整原字符串，不截断或丢Unicode |
| 同label两个按钮 | 独立ID，分别复制各自块，未点击不触发 |
| 本帧数据更新 | 复制与当前显示同一版本，不读下一次缓存 |
| 未知枚举/flag位/HRESULT | 明确未知并保留数值；组合flags不丢已知位 |
| 无数据/查询失败 | 保留原状态/错误，不用模板冒充成功 |

### 5. Good / Base / Bad Cases

- Good：flags=0x40显示完整FRAME_LATENCY_WAITABLE_OBJECT；点击画布复制只复制该帧全部画布摘要，EDID按钮复制全部屏幕。
- Base：普通S_OK、BGRA8和FLIP_SEQUENTIAL使用完整名称，图形状态和回退顺序不变。
- Bad：硬编码截图参数、丢unknown位、复制时跨线程查询DXGI、使用同一按钮ID或忘记恢复cursor。

### 6. Tests Required

- ClipboardProbe真实ImGui按钮点击覆盖尺寸/右齐/块下/ID/后续cursor、多DPI/主题/三语和中文长多行/四屏文本；不写真实系统剪贴板。
- 控件必须单独覆盖作为容器最后一项直接EndChild/End：非零ItemSpacing、root/固定child/AutoResizeY、滚动/裁剪、外层缩进、恰好/小于按钮自然宽度及分数DPI。收集真实ErrorCallback/ErrorCount且保持assert/tooltip配置不变，不能靠后续Text/Dummy清除IsSetPos而掩盖问题。
- 纯格式probe覆盖用户实际值、组合/未知flag、未知enum/HRESULT、SampleDesc与组合DXGI_SWAP_CHAIN_DESC1、RTS成功/失败；不创建GPU或窗口。
- 完整Debug ARM64 solution及适用attention/copy无窗口专项；不以纯格式测试称真实设备故障或桌面视觉验收。

### 7. Wrong vs Correct

Wrong：点击后再调用CanvasInformation取新快照，或仅复制已换行显示的可见片段。

Correct：绘制与复制复用同一局部完整字符串；只在user点击标准按钮时交给现有ImGui剪贴板接口。

## 白板拖动惯性（多设备漫游）

“绘制 → 通用”在预测设置之后、橡皮组之前新增“白板拖动惯性”，使用现有 ToggleRow + InlineCheckboxes 五类输入。总开关默认开、鼠标/笔尾默认关、触控板/触摸/笔默认开；父关闭收起但保留子偏好，子项可全关。设置Changed及启动经Host同代快照发布，owner应用，只有真实变化唤醒；热禁停对应coast而不取消held跟手，重新开启不复活旧速度。2026-11-12起PTP扩展暂时停用，触控板子项仅保留偏好，不代表当前设备能力；本轮不为停用另改UI或文案。完整字段/线程/输入/验证合同见 [canvas-pan.md](canvas-pan.md)，不要添加重启生效说明或新二级页。
