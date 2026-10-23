# 技术设计

## 边界与接口

CV 核心的 Initialize/Process 增加默认 nullptr 的 TrackerDebugSnapshot* 参数；现有 MotionResult 完全保持。公开诊断只用标准库/数值，最多 320 个原参考点及 320 个补点，固定有界检查/候选/网格数组。仅从原生产表达式实际执行位置记录，保持短路/顺序/提前返回，所有记录 noexcept。记录调用前参考序号、调用后参考序号、各阶段有效性、实际比较值和门限。诊断不分配或抛异常到生产 catch。阶段内未完成或未执行的值不能显示成零失败。

生产已存在的共识 best、indices、NonTranslation、已检查竞争运动、Coverage、出生锚点一致性用于填充诊断；不另跑聚类或估计。竞争候选如果早退只表示实际已评估范围。支持外接范围不是自动 ROI。

## 窗口

WindowRole::ScrollFollowDebug 追加在 Count 前，采用现有设置 owner thread/queue，增加独立角色的线程路由判断，不把 IsSetting 的样式/owner 行为扩大。普通 WS_OVERLAPPEDWINDOW/WS_EX_APPWINDOW，owner=null，TOPMOST。可选启动登记；不加入 overlay 链和 ready 必要条件。WM_CLOSE 经 Service.Destroy，清理槽位订阅、timer、GDI；不 PostQuitMessage。统一退出隐藏/销毁。只启动打开，内部仍支持 Service 反复创建/销毁验证。

BuildCurrentFilterList 增加角色，仍每次 Source 前由 Service 读取当前句柄并做 IsWindow/进程/child 检查，不另存 HWND，不改其余角色。

## 数据与线程

业务侧在原 Mag 回调中将实际提交 CV 的裁剪 ROI 逐行复制成自有紧凑 BGRA；不保存借用指针，不重复截图。图像与 CV 快照、生产 MotionResult、几何、身份、Capture 统计一次发布。三槽 SPSC latest-only：生产/待消费/显示各一槽，冻结保留显示槽，生产仍能交换另外两槽。最终拒绝到 Paused 后没有新 Tracker 图像覆盖。所有状态和摘要更新独立于图像，不能冲掉终止失败。

最多 30 条数值摘要，不保存完整图像历史。UI 每约 100ms 消费最新并绘制，Mag 不等待消息/GDI/导出。Debug 关闭通过轻量启用判断；像素/大型快照只在开启时存在。分配失败只丢弃/关闭诊断，不能改变 Tracker 结果。窗口关闭停止采集诊断，在途回调靠共享寿命自然结束且不发布旧窗口代次。

Capture 只补只读观测：真实采样时间、失败操作及原生错误。沿原状态更新点填充，禁止改变 Begin/Update/Read/Stop/Pump 调度。业务暂停独立记录 Capture quality/API/timeout/ROI/viewport clamp，Stop(Requested) 不能掩盖原始原因。AwaitTracking 没有执行 CV 时不得捏造结果。

## 显示和导出

GDI 双缓冲绘制，左侧等比 ROI 预览、右侧可滚动统计、底部 30 条事件，顶部 live 状态；支持 resize/分隔比例、滚轮缩放/拖动平移/复位。图像和点箭头网格统一物理像素映射；CV 处理坐标用实际 scaleX/scaleY 还原，随后只做视图变换，不再乘 DPI。标注 Capture 全区/ROI/处理尺寸和网格参考坐标语义。

点、光流、网格、支持范围可开关；箭头 1/2/4 倍仅影响显示。绿=主共识内点（不代表整帧接受），灰=近静止，橙=生产检查过的竞争运动，红=失败/排除，青=新补点，形状区分 Forward 等中间阶段。分别显示生产 delta/total 和诊断估计值。冻结固定图像和该帧统计；live 采集状态/年龄单独标识。没有新帧则保留旧图并显示 Stale、失败来源/原因，时间只代表 Read 返回，不代表 compositor 最新内容。

点击保存时深拷贝所选不可变快照（最多一个导出任务）。worker 自有 COM apartment/资源，原生目录选择，使用既有 DibSurface PNG 和 jsoncpp 保存 raw.png、preview.png、snapshot.json（含身份、validity/checks、几何、原数值、30条摘要、显示选项）。默认不落盘。导出不占 Mag/Draw3/UI3 渲染线程；取消/失败明确反馈，不影响生产。

## 兼容和范围

不增加 highgui/新图形框架/新依赖/新截图或新最低系统要求。console/verbose 独立。用户明确允许短 GUI 冒烟，工具或环境限制时仅报告事实和人工步骤。诊断关闭行为和生产结果由差分审查与短对照验证，不以合成通过替代 GUI。
