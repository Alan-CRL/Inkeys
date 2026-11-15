# P3＋P4 渲染、几何、UI 与运行期只读审查

审查日期：2026-10-17。审查人：p34_renderer_implement。先实施 renderer 与历史子块，随后按主会话要求只读审查其他 owner 的共享几何、UI、窗口与运行期接线；此记录不代替独立 trellis-check 对本人历史／持久化改动的审查。

## 已实施并冻结的本人职责

- Renderer 连续同色／同透明度／同线宽 stroke batch 共用覆盖层，允许每批具有不同虚线相位；fill 与其他材质形成顺序屏障。维持 64 字节 primitive、b4 和独立 shader 协议。
- 新增 ClearShapeRenderCache / InvalidateShapeCache；只有成功绘制的首次候选进入 CPU 渲染缓存。
- InkCanvas 同索引 ReplaceStroke / ReplaceShape；runtime 统一 operation journal、cursor、floor 与 revision allocator；编辑前后快照及普通追加在同一撤销顺序。
- 编辑 Undo／Redo 采用候选文档及旧新 tile 完整有序恢复，不使用创建时 hot preimage；成功后才安装候选。
- 白板 cold v3 包含编辑 journal、隐藏 redo、Clear 后继 continuation，以及最近 Clear 前的完整编辑历史；旧格式懒迁移。完整候选验证后才替换目标页。
- 已补真实 renderer 离屏 GPU 测试与生产 CPU／cold 历史测试。本人未运行构建或 EXE，统一验证由主会话负责。
- 修改文件 UTF-8 BOM／CRLF 保持，限定 git diff --check 通过。无 GUI、commit、push。

## 已发现问题与 owner 处理状态

| 问题 | 证据与影响 | 责任方／状态 |
| --- | --- | --- |
| 编辑栏粗细预设跨线程读取 | Bar.ShapeEditor Configure 在 render 线程更新 widths，而 Activate 从交互线程读共享数组，且未绑定成功呈现帧 | UI owner 已修：widths 放入 Frame，Activate 只读呈现快照；已静态确认调用变为 frame.widths |
| focused 数据竞争 | Keyboard 原先无锁读写 focused，Configure／Pointer 则在 inputMutex 内修改 | UI owner 已修 atomic focused，待统一构建与并发检查 |
| 最小化／恢复缺编辑窗唤醒 | Freeze.whiteboardMinimizedChanged 原先只请求 Bar／Freeze／PageControl；Overlay Hide 后 Idle，恢复可能沿用失效输入帧 | UI owner 已确认补 SelectionOverlay Notify；待统一离屏／窗口合同验证 |
| 主栏位置变化未更新浮条避让 | Overlay 原先只因 drawing snapshot／display 刷新，主栏移动不会定向触发 | UI owner 已确认仅 Bar 成功 bounds 变化时唤醒 Overlay，避免稳定光影相互续命 |
| 高 DPI 小工作区浮条不可达 | 固定 460×420 px @ 2x 时，换行属性、边数、色板总高可大于工作区；只 clamp screenY 仍使底部出屏。原测试 work 随 scale 扩大，未覆盖此情况 | 已发 UI owner 与主会话，需受限扩展视口／可达策略及固定小工作区测试；本轮审查结束时未验证完成 |
| 六类空间 UI 测试参数非法 | RunShapeEditorOffscreenTest 原先全部传 SolidConstruction{非零 depth, 非零 height}；Cuboid／Cube 需要 height=0，其他空间图形需要 depth={}，导致构造失败并跳过 scene | 已发 UI owner，需修合法夹具并确认所有 21 类真正进入 UI 测试 |
| Cube 非等 dx／dy 的端点错位 | 首笔预览与最终面按 max 边方形化，但构造 c.end 保留 raw end；BuildEditHandles 1001 直接用 c.end，可落面内，例如 (0,0)→(100,30) 的实际面为 100×100，控制点仍在 (100,30) | 已发 core owner，需规范化参数／句柄与端点编辑，并加四方向回归 |
| 多阶段旧提示命令缺 generation 校验 | 原 constructionAction 只核 scene／session／id，旧 Back／Complete 可作用于后来确认的顶点 | core owner 已修非 Cancel 动作核当前 published identity，已静态确认 |
| Fatal 封口可能保存未完成步骤 | 活动 Arc 首步候选是 Circle，空间首步是 Rectangle；Fatal 原路径直接 FinalizeRuntimeShape 可误当最终对象写入 | core owner 已补防护，并准备以显式 completed 标记区分完成与取消；需要真实 producer／Fatal probe 验证 |
| 活动构造期间取消／Undo 屏障 | 原 CanvasCommand 只在 active.empty 处理，Esc／切页／Clear 可能等待 Up 后先提交 | core owner 已补 active 控制屏障、FIFO deferred 文档命令、Undo 标记本次 drag discarded；待生产 probe 验证 |
| 异步权威安装未统一清理编辑缓存 | SetPage 白名单已清缓存，但 PresentationLoad 等直接走 restoreAfterDocumentSlotSwitch；该统一 helper 原未清 shape cache／关闭会话／推进 documentGeneration | core owner 已确认首轮冻结构建结束后修统一入口；安装后应 close(...,false)，防旧 preview 覆盖新页 |
| 自动编辑 pending 在 GPU 提交前发布 | normalUp 在 DrawStoredStroke／ApplyOperatorLayers 前设置 pendingObject／pendingItem，submitted=false 仍可能发布编辑框 | 已发 core owner 与主会话，需成功 resolve 后才开放编辑，或等待权威恢复；文档 CPU truth 仍可保留旧恢复策略。手动 ConstructionComplete 已在 GPU 成功后设置 pending，正确 |

## 已确认的一致性

- v2 旧箭头解码设置 legacyArrow，v3 再保存显式保留该规则；新箭头依线宽计算头部。
- Compound 标准部件引用的 version 与带构造根 v3 一致，稳定 ID／revision 由根与引用共同验证。
- Overlay WinProc 的 Hit 从 inputMutex 保护的成功呈现 Frame 读取；绘制线程不直接在 UI 中修改文档。
- Window Service 已纳入 SelectionOverlay 的 popup／owner／置顶／最小化与反向销毁角色；没有第二套 RTS。
- 新图形类别通过统一能力查询启用 21 类，后置 15 类继续无业务 preset。
- 常规 Up 在 finishShapeContact 前将视觉端点回退到 raw Up；FinalizeRuntimeShape 的相同端点短路避免预测点进入最终文档。

## 交接与验证边界

主会话负责最终 ARM64 solution 构建、真实 GPU、共享核心、UI offscreen、冷页与 Headless 验证。以上“owner 已修”只表示源码修改／owner 确认，不代表测试已通过。本人审查没有写 Controller／core／UI；本记录是只读审查的唯一新增文档。后续独立检查应重点复核本人 operation journal、cold v3 全分支恢复、Clear 前后完整历史、GPU 回滚，以及各 owner 对本表待项的实际生产接线和失败测试。


## 主会话闭环（2026-10-18）

上述审查时的待项现已由各owner与主会话修复。界面终结意图采用最多5项、同场景按种类合并的surface tick续接；绑定PPT→isolated门禁、Clear前Replace redo、冷页重复身份/映射负例均纳入生产测试。shapes.md已补当前合同和防回归。最终完整ARM64构建退出0；生产core/Interaction/EditHistory/376 GPU cases/100页storage/380 Editor场景/3024 Bar配置以及Headless、UInk、i18n均通过。实际命令与退出码见 `../validation.md`，本段由执行测试的主会话补记；不把原审查代理的只读工作冒称为其运行测试。
