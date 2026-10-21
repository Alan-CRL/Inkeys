# 共用原定格 Mag 的窄范围读回设计

## 当前范围与历史

本轮用户明确取代独立 Capture-Only 路线：删除 MagCaptureService、MagnificationRuntime lease 及相关旧测试登记，撤回未完成的通用生命周期接线。旧原型与探针仅作为历史证据保存在 research/ 和已提交的 f07b562e 中，不是当前接口合同。

只在 Inkeys.exe 内、原定格 Mag 上执行 PrintWindow(2) 诊断。普通 Freeze、WindowService、Draw3、Bar、文档与保存逻辑保持既有分工；不新增连续捕获服务或通用 owner RPC。

## 既有线程设计保持

Inkeys2 的 2025-02-15 提交 0c0aaf0e99a4d5ced0902c1d692855ef61449f96 将初始化与窗口创建封装在 Mag 窗口线程，并用 promise/future 等待就绪；Inkeys3 的 WindowService Overlay STA owner、CreateGroup 和 readyPromise 保留这一关键安排。因此不需要再次修改普通定格线程。

后续 Filter/Source/Redraw 的协调线程分工在 Inkeys2 已存在，本轮也不重排。新增诊断由原 Host 的消息/定时消息运行，保持 owner 消息泵；初始化、创建、回调登记/恢复、读回与反初始化的实际线程 ID 都记录，普通定格前后对照仍走原 MagnifierThread。

## 复用与图像

直接调用 PrepareMagnifierWindow、MagnifierHostCreated、MagnifierChildCreated，以及 Win32MagnifierOperations 的 PrepareHidden → SubmitFilter → SubmitSource → Invalidate → Redraw。原角色排除列表覆盖 Mag、双画布、主栏、设置和分页等已登记窗口，不把它描述为枚举所有未知同进程窗口。

Host 先 alpha=0 再显示以保留绘制生命周期；不最小化，不在读回期间 SW_HIDE。读回使用原 child、复用 DibSurface、PrintWindow(child, dc, 2) 与 GdiFlush。记录实际 child client 和 MagGetWindowSource，保留原定格高度少一像素、Source 宽高少一像素的现状，不伪报完整主屏尺寸。

无回调更新/等待/读回是必需路径，回调只作可选候选。测试先在未登记回调的原冷窗口完成静止及连续变化图案，再登记候选，最后恢复无回调验证继续更新。

首次等待较长、稳态短等，以定时消息继续处理更新；固定等待只是延迟策略。无可靠完成信号时不承诺最新帧。同图可以是静止；变化图案须最终读到对应新像素，API 成功不能替代这一检查。

## 回调候选

动态尝试 MagSetImageScalingCallback / MagGetImageScalingCallback，保存并恢复原回调。回调只进行必要的同格式直通与原子计数，不作为图像来源，不保留借用图像、不等待或写日志。记录登记结果、实际触发线程、进入/退出及 PrintWindow 匹配关系。失败或没有触发则有界等待回退；不关闭 DWM，不新增 Win7 图像导出能力。回调退出计数只用于实验，不能提升为最终整帧呈现保证。

## 软件内受控验证

显式 --freeze-mag-printwindow-test，关闭 harness 或非法参数必须直接退出；普通启动不进入诊断。入口先于配置、单实例及普通产品初始化，不打开用户文档或设置，不写真实配置。使用真实 WindowService 角色和定格回调创建唯一一对 Mag，而非独立复刻初始化。

专用源窗口显示唯一阶段的网格纹理；Bar 角色蓝色覆盖窗有屏幕正对照，确保真实遮挡。原 SubmitFilter 后分布核对像素，分别比较未登记回调、登记期间、恢复后的更新；静止重复读回；原定格显示/隐藏前后对照。父 Host 使用原 DefWindowProc，避免与源同色绘制造成假阳性。测试不启动 WGC，不设置禁止捕获属性；专用窗口自动关闭，输出到临时目录。

若普通定格与新增 owner 诊断结果不同，分别报告证据，不修改既有线程使结果看起来一致。初始化、像素、排除、回调和清理失败须各自可追溯。测试进程的最终超时退出不算正常资源清理通过。

## 交付边界

本次是软件内读回验证，不是完整滚动批注或可复用异步捕获会话验收。Tracker、穿透、Draw3、按钮和 UInk 仍未进入。当前实际构建与像素结果写入 implement.md 和 research/；父任务矩阵由主会话维护。本轮不自动 commit/push。
