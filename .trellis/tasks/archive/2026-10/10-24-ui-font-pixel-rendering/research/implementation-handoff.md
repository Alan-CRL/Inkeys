# Implementation handoff

日期：2026-10-24。仅实施主栏/控件字体与实验设置入口，没有改变设置字体、启动预览、抗锯齿模式或第三方实现。未执行commit、push或GUI。

## 实现

- `Other.Config.cppm` 新增 `Experimental.Inkeys3.UI3.AllowFractionalFontSize: IdtAtomic<bool> = false / NoUpload`，复用严格bool、复制、Mini和默认机制。
- 新 `UI/Bar/Bar.FontSize.h` 为生产与Headless共用纯数值函数。最终像素em高度floor后反算字体大小；小于1px返回0，不round、不钳到1px。
- `Bar.Rendering.cpp/.cppm` 冻结每renderer策略，解析DC的有效Y轴变换及DIP/PIXELS模式，Word/细刻度复用同一函数；细刻度缓存增加实际字号键并在切换时失效。字体格式缓存已按实际格式字号区分。
- `Bar.RenderLoop.cpp` 接入细刻度、dock indicator（额外变换后取格式）、FPS；帧首策略变化设置真实damage和visual retry。FPS、dock与Word均保留/加入0px/null格式保护。
- `Bar.Scene.cpp` 在相同frameTime早退前处理策略及FullDamage，覆盖分页和隐藏恢复。
- `Setting.cpp` 只新增实验卡与原子值写回、主栏/四分页轻请求、FIFO保存，增加115DIP父panel高度；复用现有控件和字体渲染方式。
- 三语新增准确标题与即时生效/向下取整说明，sync生成header和基准快照。
- 保留MeasureText现有未截断布局上界，图标/几何继续原有连续缩放。

## 回归

- `animation_tests.cpp`：同源纯数值函数覆盖48DIP=0.6、DPI/有效缩放矩阵、不放大、开启保留原尺寸、小于1px和非法输入。
- `config_metadata_tests.cpp`：真实schema/JSON默认、缺失/非法值、双向写盘/重读、Mini隔离、复制冻结、Reset和NoUpload。
- `Bar.EraserAttribute.Test.cpp`：已有无窗口离屏入口新增生产Word与独立推导字号的精确BGRA对照，覆盖内容缩放、DPI、非均匀DC缩放、PIXELS模式及开关双向缓存；真实细刻度DWrite字号/缓存、旋转剪切Y轴、Scene相同时间戳和隐藏时policy产生新damage。隐藏测试先消费SetOpacity几何damage，避免断言被遮蔽。

## 已执行

- `pwsh -NoProfile -File Scripts/i18n.ps1 sync`：通过。
- `pwsh -NoProfile -File Scripts/i18n.ps1 check`：英文/繁中均437/437通过。
- `git diff --ignore-submodules=all --check`：通过。
- 14个受改产品/测试/资源文件与 `git cat-file --filters HEAD:<path>` 对照BOM和换行种类：全部保持原有CRLF及BOM状态；新增header为UTF-8无BOM/CRLF。
- 静态搜索确认Bar所有四个实际DrawText/DrawTextLayout入口覆盖新策略，设置未改字体图集或文字尺度。

## 交给主会话

- 使用已准备的ARM64原生MSBuild完整 `InkeysRepo.sln Debug|ARM64` 验证；本代理按约定未运行构建。
- 新产物 `InkeysHeadlessTests.exe --no-window`。
- 新产物 `Inkeys.exe --bar-eraser-offscreen-test`：原有早期CLI，不创建任何HWND，新增字体回归包含其中；详细报告 `Build/eraser-b/offscreen-results.log`。
- 最后修改产品文件为离屏测试文件，静态检查后无进一步代码变更。真实大屏清晰度没有GUI证据，仍由用户人工观察。

## 编译后访问控制修正

主会话首次真实编译发现新增离屏测试直接调用受保护GetThicknessFineDialLabelLayout而报C2248。trellis-check已在测试块内派生FontTestRenderer，public using继承构造函数并公开该测试入口；不扩大生产API、不添加friend、不改module。仅该测试块有源码修正，保持原UTF-8/CRLF；请主会话复编再运行离屏回归。
