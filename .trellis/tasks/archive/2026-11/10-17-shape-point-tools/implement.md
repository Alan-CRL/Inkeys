# 逐点折线与任意多边形 执行清单

依赖P3底层接口与提示已接线后正式激活；准备性共享接口可并行实施但不提前启用UI。核心→runtime真实输入/预测→UI参数→保存/撤销→生产CPU/GPU/离屏验收。全部完成后记录validation；P4后置条目留未完成。构建/禁GUI/格式/无commit沿P3清单。


- [x] 共享typed构造与参数控制点接口。
- [x] 生产跨接触raw/predicted与步骤取消/回退/单次提交。
- [x] 既有工具启用、公共UI3步骤提示与当前对象编辑接线。
- [x] UInk v3、白板cold v3、统一编辑journal/UndoRedo/Clear。
- [x] 独立审查及生产CPU/GPU/离屏/保存验证，详见validation.md。
