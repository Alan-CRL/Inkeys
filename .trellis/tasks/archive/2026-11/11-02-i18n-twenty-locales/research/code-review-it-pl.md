# 全范围只读审查（翻译工作完成后）

已读取trellis-check、当前PRD/design/implement、check.jsonl、native-desktop索引与i18n/FreeType摘录、C++/构建/复用规范，检查当前全部C++/RC/resource/工程与测试diff。遵照根会话独占构建约定，没有执行构建、产品测试或修改其他代理文件。

## 当前结论

- 未发现排序映射、稳定保存索引、LANGID/RC资源绑定、字体选择、同族图集复用和跨族重建的实质源码缺陷。
- DisplayOrder为同一Locales元数据产生的纯显示索引；设置页显示行回写经LocaleIndexForDisplay，不把de-DE的首行当成en-US旧保存值0；无配置迁移。
- 新语仍Harmony现有资源，未增加字体或重做渲染管线；ApplyPreparedSettingLanguage只抽取原决策供生产和专项共用，失败语义保持。
- 默认按钮新增七个主语言，en-GB/es-MX沿原PRIMARYLANGID分派；测试有独立全部四按钮预期和自定义部分覆盖。
- 真实资源/format/并发及生产字体生命周期循环自然扩到20语；页面和独立样本矩阵已扩展分数倍率。

## 已反馈的验收覆盖缺口

`InkeysHeadlessTests/setting_design_tests.cpp:5821`与`:6208`当前只渲染/滚动菜单并确认选择不变，`Inkeys/IdtI18n.cpp`测试映射往返。尚未以真实选择动作证明排序后的显示行投递到正确的语言请求/保存索引，例如首行de-DE不能变为旧保存值0=en-US。建议在现有无窗口控件夹具或共享生产投递路径补此项。根会话已收到通知；没有为此在构建期间改文件。

## 边界

该审查为静态证据，构建/20语实际资源、字体/布局和失败专项结果由根会话统一执行并记录。若本报告后代码继续变化，应复核新增diff。

## 后续补齐（根会话明确授权后）

已在现有CheckLocaleTextAndSelector内增加一段仅en-US/100%执行的真实ComboRow事件回归。路径：初始化控件焦点→io.AddKeyEvent F4→Home/End及Down→Enter→控件Selectable返回changed→LocaleIndexForDisplay生成请求与保存索引。固定预期行0/1/2/19分别为de-DE/en-GB/en-US/zh-TW（保存7/19/0/2）。检查导航不提交、Enter恰好提交一次及实际身份，不直接修改selected或伪造NavActivateId。测试中的saved/requested是捕获控件输出的局部观察值，不执行真实磁盘配置I/O。

原无BOM/CRLF已保持，修改完成后已交还文件所有权供根统一最终构建；本人未运行build/test。前述覆盖缺口已由此变更补齐，运行结果仍待根验证。
