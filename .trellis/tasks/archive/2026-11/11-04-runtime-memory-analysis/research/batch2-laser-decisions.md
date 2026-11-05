# Batch2 Laser生命周期实施边界

## 已确认现状

- Renderer.cpp CreateSizeDependentResources无条件建立两张Laser主体；ordinary合成仅使用L0/L1/L2，Laser resolve是独立后续pass，允许非Laser无主体。
- ClearAllLaserCoverage清色并释放bake scratch，不释放两张主体或live；SRV/RTV引用须由owner解绑。
- WindowController.SetActiveTool与SetSelectionMode均唤醒绘制线程。选择模式不接受新绘图；ActiveTool可能记忆Laser，因此有效资源需求按Laser且非Selection判定。
- 旧Hold/Fade可继续保留可见Laser，但用户现已明确离开工具销毁，所以离开时结束临时会话/接触、脏化旧区域后全帧重绘；不改变选中Laser时的正常Up→Hold→Fade动画。

## 最小变更

1. Renderer.cppm/.cpp新增EnsureLaserResources/ReleaseLaserResources/LaserResourcesAvailable/ResidentBytes，候选组全部成功后原子替换。CreateSizeDependentResources不分配Laser；Resize仅在原来选择过Laser且有主体时重建，普通resize与新device初始化不无条件建回。
2. RendererLaser.cpp的增量层创建先要求主体可用，正常非Laser不通过增量路径偷建主体。小shader/固定buffer不改，L0/L1/L2与HLSL布局/像素公式不变。
3. DrawingController在工具/Selection控制边界同步资源。离开只取消Laser runtimes、DiscardUntilTerminal并沿既有退还路径回收，不影响同时存在的持久工具历史；清CPU tail/lifecycle、pending bake、粒子和旧dirty，再释放；请求full present以去掉旧激光画面。准备失败只拒绝本次Laser输入，不使普通绘制失败，不每帧重试分配。
4. 设备恢复及resize后重同步当前工具。现有预热函数在无Laser时只预热小对象路径，选择并建立主体后预热完整Laser路径，避免首笔JIT。

## 验证

复用真实WARP离屏资源和真实像素读回，覆盖非Laser空状态、两张主体按当前尺寸建立、增量/bake存在时全部释放、绑定view解除、普通笔像素不受影响、重入像素一致、尺寸变化及候选中途失败回滚。测试源码限制在Renderer owned实现，主会话负责构建/运行；HiddenWindowTest由ULW代理显式在Laser段建立主体。文件保持UTF8 BOM+CRLF，保留上一批capture move。禁止改device/层架构、历史缓存/保存去重、IdtMain或其他代理目录。

## 已实现接口与验证入口

- Renderer新增EnsureLaserResources(UINT,UINT)、ReleaseLaserResources、LaserResourcesAvailable、LaserResourceBytes。bytes仅为实际持有纹理描述载荷。
- 新CLI由Setting代理在IdtMain注册：--draw3-laser-resources-test → RunLaserResourceLifecycleValidation；Debug harness关闭/坏参数早退。使用真实WARP、无HWND composition swapchain、实际Init/Resize/Release/恢复、staging像素读回；输出ordinary_init/selected/live_and_bake/left/selected_resized/recovery_left的resource_bytes。覆盖候选部分分配失败原子回滚、绑定释放、3次重入像素一致、普通笔像素无残影、resize交集像素保留。
- Controller在control wake后与frame开始/resize后同步，离开仅取消实际Laser runtime；Laser所选但已提升为持久Eraser的掌擦保留原有Move/Up/route并正常入文档历史；选中失败不每帧重试。普通自然Up→Hold→Fade不变，切离按用户明确方向结束临时会话。
- 已按辅助ULW代理要求在末尾dirty clamp后补已有非空dirty的full-present提升，避免新DIB读到未完整合成的旧backbuffer；不因回收制造空闲帧。
- 本代理只做静态/格式/diff检查，未编译、运行、打开GUI或修改系统配置。构建与真实探针由主会话统一执行。
