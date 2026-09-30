# F-021 底栏 seqlock 独立审查

## 范围与结论

静态检查 `Bar.BottomDock.h`、`Bar.RenderLoop.cpp`、`Bar.Main.cppm`、`bar_bottom_dock_tests.cpp` 的当前工作区变更，并用全目录搜索核对 `Bar.Interaction.cpp` 等实际调用方。未运行构建、性能采样或 GUI；主任务负责串行执行验证。

生产代码中，两组偶数/奇数版本的新增 fence 位置符合 `rendering-and-ui.md:890-892` 的约定；未发现 F-021 修改自身引入的漏锁、漏 fence、ABI 变化或可确定的业务语义回归。审查中发现的并发测试零样本调度缺陷、诊断缺口和缩进问题已由主任务修复。新增 fence 位于逐帧/命中快照热路径，是否达到性能专项的噪声门槛仍需主任务的可比采样证明，不能仅凭静态审查认定为零成本。

## 写者、读者与所有权

| 版本 | 写者 | 读者与保护 |
| --- | --- | --- |
| `bottomDockTransitionSerial` | `Bar.BottomDock.h:1322-1358` 的 `BeginBarBottomDockTransition` 以 CAS 独占奇数，`TryBeginBarBottomDockFrameTransition` 仅在帧消费的偶数版本仍当前、且未拖动时领取；`FinishBarBottomDockTransition` 在 deferred barrier 与载荷后 release 发布偶数。调用方包括 `Bar.Main.cppm:421-431` 折叠，`Bar.Interaction.cpp:6163-6178,6269-6288,6301-6307,6540-6553,6668-6687,6710-6728` 输入、屏幕屏障与回滚，`Bar.RenderLoop.cpp:7553-7559,7755-7764,13134-13144,13163-13176` 自动阶段、PPT、白板写回。 | `Bar.RenderLoop.cpp:13181-13211` 取得同帧两轴/抓手/位移 tuple；`12591-12611` 在 ULW 前复核版本、barrier 和位移。前者在全部 relaxed 载荷后、最终版本读取前有 acquire fence；后者在 deferred/presented 载荷后、最终版本读取前也有 acquire fence。`ResolveBarBottomDockFramePresentation` 对奇数或版本不一致返回 deferred（`Bar.BottomDock.h:1397-1408`）。 |
| `bottomDockPresentedMappingSerial` | `Bar.Main.cppm:793-826` 的直移重基准由 `Bar.Interaction.cpp:6566-6592` 和 `Bar.RenderLoop.cpp:13060-13100` 在 `directWindowDragMutex` 内调用。成功 ULW/EndDraw 的完整映射由 `Bar.RenderLoop.cpp:12567-12568,12717-12898` 在 `BarWindowPresentationTransaction` 持锁期间写入；该 RAII 类型在构造时取得同一把 mutex（`Bar.WindowGeometry.h:12-25`）。两处写者均在奇数 `fetch_add(acq_rel)` 后、relaxed 载荷前有 release fence，末尾以 release `fetch_add` 发布偶数。 | `Bar.Main.cppm:524-625` 的 `BottomDockPresentedSnapshot()` acquire 读偶数、读全部 relaxed 字段、执行 acquire fence、再次 acquire 读版本，仅相等才返回。交互、绘制和命中均通过该快照；ULW 前单独读取的 `directWindowPresentedTranslationX/Y` 与 `bottomDockPresentedTransitionSerial` 在同一 `directWindowDragMutex` 内（`Bar.RenderLoop.cpp:12567-12611`）。 |

两组版本均先将奇数领取与后续载荷读写排好序，最后通过 release 偶数提交。`Bar.BottomDock.h:1332,1347`、`Bar.Main.cppm:801,622`、`Bar.RenderLoop.cpp:12599-12605,12748-12752,13206-13211` 的 fence 与版本校验成对。新添的代码未改变这两个 serial 的字段类型、类布局或函数签名；F-021 以外的同文件变更不在本审查结论内。

## 复审结果与测试边界

- **没有剩余的 F-021 生产代码问题。** 两个 presented-mapping 写者受同一 mutex 保护；transition 写者以 CAS 排他领取版本。全部相关读路径均在载荷读取后设置 acquire fence，相关写路径均在奇数领取后设置 release fence。
- **并发样本门禁已修复。** `bar_bottom_dock_tests.cpp:245-299` 让两发布者在第一次完成偶数发布后，于临界区外等待读端确认；读端只在 `before > 2 && after == before` 时确认并计数。10 秒 deadline 的退出路径先放行等待者再 join，并对超时明确断言。正常调度且未触发超时的成功路径中，至少一个被接受样本必来自实际发布；每八次发布后的 `yield()` 仅增加后续竞争机会。测试在 tuple 错误、零样本、超时或终态版本不符时输出计数、超时状态、最终版本及首个错误 tuple（`bar_bottom_dock_tests.cpp:302-318`）。
- **直接覆盖的边界。** 该 headless 测试模型化 `bottomDockTransitionSerial`，没有直接实例化 `Bar.Main.cppm:524-625` 的已呈现快照及其 `bottomDockPresentedMappingSerial`。此路径已按实际写者锁作用域与读写顺序作静态审查；运行期窗口/ULW 行为仍属主任务的手工或集成验证边界。不要仅为映射第二个 serial 复制一份与生产实现相同的合成测试。

## 验证边界

已完成 `rg` 全仓 serial 引用和写者调用点搜索、锁作用域核对、针对四个目标文件的 `git diff --check`（通过）。未执行编译或运行测试，以避免与主任务的构建/采样并发。未观察到与 F-021 对应的独立运行数据，因此该审查只证明源码路径与锁/内存序结构符合约定，不能替代 ARM64 编译、实际并发执行或性能对照。
