# 状态与 UI 统一设计

- 先列状态字段、authoritative/requested/applied、写入者/线程/副作用，再决定是否迁移；不把相似字段机械合并。
- 现有业务入口是候选，不预设正确；逐调用点验证 mode、pen 参数、Draw3 bridge、UI 高亮、光标、窗口穿透和配置更新。迟到异步结果须保留 revision/generation 条件。
- 必要的职责拆分按 coordinator、command/state、layout、animation、resources、lighting、input 边界；新 .cppm/.cpp 同步项目登记，不用 .inl 藏超大实现，不生循环 import。
- UI3 同层复用现有 widget/capture/hover、主题/DPI、dirty/cache。ImGui 设置、Bar、Draw3 继续各守 owner；不做整仓框架迁移。
- 先冻结主 agent 唯一写入的 IdtState、RenderPipeline、WindowService、工程/公共测试接口；worker 只写明确不重叠的调用点。
