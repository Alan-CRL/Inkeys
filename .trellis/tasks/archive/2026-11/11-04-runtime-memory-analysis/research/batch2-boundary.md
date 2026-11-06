# 第二批：已批准2–5的实施边界

用户已明确批准开始处理2–5；1保存峰值去重、6历史缓存页策略留待后续。目标为普通静止及使用功能后回到桌面的稳定内存下降；允许初始化/解码/恢复的暂时峰值。上一批6个源文件修改必须保留，本批基线为validation/after-bin与validation/batch2/before-identity.json，源码原字节在batch2/source-before。

## 不变约束

- L0/L1/L2语义与架构、UI共享WARP及Draw3独立hardware-first/fallback device分工不变，不合并设备，不改变正常画质、帧率、Undo/PPT/白板内容和兼容范围。
- 资源准备/回收在原owner线程的控制/功能切换边界执行，不在每个Move或每帧重新分配。不用Computer Use，不绕过锁屏，不打开Office/用户文档，不commit/push。
- 原UTF-8/BOM/CRLF、适量中文关键注释及最小diff。已有未提交代码属于本任务上一批，禁止撤销。

## 2 Setting CPU图片恢复副本

以资源ID/尺寸/像素转换规则替代持久BGRA副本。普通显示沿用热纹理，epoch恢复临时解码/转换/上传后归还CPU像素。保留HICON槽0的正确恢复；不引入第三方依赖/新平台API，不重构字体/ImGui整个session，不改变像素变换及可选atlas降级。测试需真实离屏重建并检查像素/slot/失败路径，字体测试不能替代图片恢复测试。

## 3 Laser工具生命周期

选中时建立大纹理组，离开时收尾临时Laser会话并解除绑定/释放；普通工具不持有主体、live/scratch大纹理。处理已有接触/尾迹/烘干与pending frame，在绘制线程控制边界完成。普通合成、resize和device recovery不无条件建回Laser。小型shader/状态可保持现有方式。需覆盖选中/离开/重入、非Laser合成、真实像素与失败/resize/恢复；不得用长期热缓存替代用户策略。

## 4 辅助Selection ULW

按真实呈现用途分离HWND/metadata与staging+DIB像素资源生命周期。辅助输出仍承担当前可见墨迹时保留需要的数据；切到Primary/白板、或完成无内容透明/隐藏交接后才回收。保持ready/revision/auxiliaryFullFrameClean证明、真正系统穿透、旧表面到新表面的原子交接和失败回退；不得伪造成功Present/清洁帧。无需本批改成新的GPU呈现架构。普通空白选择模式是要重点覆盖的目标，避免每帧释放后又重建。

## 5 Freeze/白板/隐藏UI

Freeze保留启动首帧/进度合同，闲置时归还自有像素；功能开始在原线程准备，结束并成功透明/隐藏提交后回收。白板借用Freeze HWND期间不能清空其画面。白板退出后，确认隐藏/提交退休与无pending submission，释放该client全屏target，保留文稿/主题/共享device，再入先重建后显露。Bar/PageControl仅在动画结束/真正隐藏后收缩明显超出需求的大backing，不对小型高频基础表面做每帧卸载；如已有充分回收则不强行改动。

## 协作所有权

- Setting代理拥有Setting实现/接口及IdtMain.cpp中的新CLI注册；其他代理只提供需要注册的函数名/声明，不能编辑IdtMain。
- Laser代理拥有Renderer实现/接口和DrawingController.cpp；辅助ULW若需要Controller调用点，通过Laser代理协调，不直接写同文件。
- 辅助ULW代理拥有TransparentPresentation/Host/IdtState等必要呈现合同；不得改Laser/Setting/Freeze逻辑。
- 第5项单独代理在槽位可用时实施，拥有IdtFreezeFrame.cpp、Whiteboard和必要Bar/PageControl文件。
- 主会话统一构建、运行、采样与最后集成；各实现代理不得并发启动构建或全产品，避免竞争与污染基线。

## 验证与交付

冻结当前版本后实施，产品编译依旧完整InkeysRepo.sln Debug|ARM64、ARM64原生MSBuild、同次PATH规范化、至少5分钟预算；只在共享TU变更时扩大独立solution范围。用既有无窗口/离屏测试补足真正资源生命周期回归，必要时增加最窄的Debug harness入口并保留release早退门。前后记录冷启动和各功能退出后的稳定值/资源账；临时峰值单列，不将载荷当实测Private Bytes。流畅度仅静态审查，正确性与内存需要实际验证。
