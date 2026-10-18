# 迁移设计

## 执行约束

按用户本轮明确指定范围从dev手工提取生产变更，不cherry-pick含采集体系的整个诊断提交。保留原文件BOM/CRLF、最小diff和关键中文注释。

1. 渲染实现代理负责GraphicsInitialization、Renderer及Laser/Primitives、TransparentPresentation、Other.Config、Setting和i18n；以dev为底移植22f7f3c9功能，去掉采集参数与统计代码。
2. 输入实现代理负责RTS/窗口/Host/Product及两个无窗口测试；以dev为底移植e8dd090c功能，保留dev原有诊断合同并去掉新Performance依赖。Host中图形偏好接线由根代理最后统一。
3. 根代理负责IdtMain、Startup.Progress、工程登记、规范、无窗口CLI接线及集成验证。
4. 独立check审查完整dev diff，重点诊断依赖残留、结构上传容量/资源用途、RTS清理期限、鼠标停止排空和配置i18n一致性。

## 证据界限

原诊断分支测试通过不能替代迁移后的重新构建。实际故障设备、真实RTS和窗口捕获继续列为现场待验，不启动产品GUI。普通失败阶段日志保留，新增性能追踪/导出不迁移。
