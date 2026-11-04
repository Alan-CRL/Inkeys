# 技术设计
在设置主题适配层集中解析材质状态，保留基础纯色 PaletteForTheme 语义。生产 ApplySettingTheme 将当前 BackdropMode 传入调色入口；默认调用保持 Solid 以兼容其他消费者与现有 tests。材质化只作用于大表面，单独给 popup/overlay 保留不透明底色，避免复用 cardSecondary 后让菜单内容直接透字。保留所有公开控件函数签名和字体/布局。原生背景切换同步主题帧边界处理；现有 HomeEntryFrameKey.backdrop 已能区分模式，审查 theme/cache revision 是否完整覆盖调色变更，只有证据存在缺口才新增字段。无需新增渲染纹理或模糊过程。
