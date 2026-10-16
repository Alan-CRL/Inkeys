# 技术设计

沿用已批准的上一轮七项计划。产品与配置启动快照、renderer资源上传、诊断矩阵/包分别独立所有权，统一集成构建。

## 边界与接口
- Renderer::Init末尾新增bool enableZhaoxinCompatibility=true；从实际device的adapter取VendorId，driver必须Hardware且vendor0x1D17且开关true才DEFAULT。其他能力查询保持。
- HostStartOptions新增enableHardwareAcceleration=true、enableZhaoxinCompatibility=true。Facade读取配置为启动快照；重建不读新配置。
- InitializeGraphicsDevice末尾新增bool enableHardwareAcceleration=true；false不尝试硬件。TransparentPresentationOptions桥接compat bool到renderer.Init。
- Renderer专用256B composite buffer/SRV和TraceViewResource/retire/cleanup齐全；仅合成t0改绑定，不改shader。
- 大结构化资源上传集中最小helper，DEFAULT通过字节box更新，所有caller offset0；dynamic仍旧mapType/head语义。UpdateSubresource是void，诊断Copy/Other叶区间真实标识，不能吞设备丢失或保留伪成功proof。
- 初始化事件hardware_acceleration_preference(detail0=requested)与structured_upload_policy(detail0=compat requested,detail1=compat effective,detail2=usesDefault,detail3=adapter identity available)用于v3报告。实际driver/vendor既有字段保留。

## v3对照
原40case语义及v1/v2可读保留，新增固定成对profiles覆盖优化普通与compat路径，均带独立256B合成buffer。Probe为独立离屏shader合同实现，不声称完整生产Host重放；报告区分CPU leaf、GPU区间、drain。用户在配置内开/关compat完成两次真实产品采集，README给同动作及开关/实际策略核验。启动不改用户持久设置。

## 协作所有权
renderer代理：Renderer.cppm/cpp、RendererPrimitives.cpp、RendererLaser.cpp、必要无窗口上传验证头/入口（和probe代理协商）、资源身份。不得修改settings/Host/ResourceProbe/Performance核心。
settings代理：Other.Config.cppm/.cpp所需测试、UI/Setting/Setting.cpp及i18n源/生成物、IdtDrawpadFacade、Host.h/cpp、GraphicsInitialization.cppm/cpp、TransparentPresentation.cppm/cpp；与renderer接口按上文对齐。
v3代理：Scripts/Draw3Performance/**、ResourceProbe.cpp/.h、必要Performance.h/.cpp版本/导出代码、无窗口probe验证；不得改Renderer/Host/settings。
主会话：计划、跨代理协调、构建/真实CLI、最终spec与验证/交付；产品文件仅为集成必要修复。

## 风险
普通dynamic在兆芯仍可能慢，所以离屏worker必须有外部deadline并继续其他case；两侧配置/工作不相同不得判winner。DEFAULT提交更快不保证完整产品帧率。子模块Git权限与Python WindowsApps运行属于环境，不能污染工程绕过。所有文件保留原编码/EOL；不修改未授权第三方。