# it-IT / pl-PL / uk-UA / ro-RO 交叉翻译审查

按 trellis-check 审阅当前任务与i18n合同；本次为只读翻译审查，不修改JSONC，不运行sync、构建或GUI。

## 具体修正建议

| 语言／key | 源文要求 | 当前译文片段 | 建议与原因 |
| --- | --- | --- | --- |
| it-IT `SettingsUI/PlugIn/DesktopDrawpadBlocker/WindowTrackingNote` | 支持窗口追踪；en-US window tracking | `il rilevamento delle finestre` | 改为 `il tracciamento delle finestre`；rilevamento指检测，tracciamento更准确表达持续追踪，不与发现窗口混同。 |
| it-IT `SettingsUI/Sponsor/Thanks` | 联系作者添加“昵称”和赞助金额 | `il tuo nome e l'importo` | 改为 `il tuo pseudonimo e l'importo`，保持请求的数据为昵称，不暗示真实姓名。 |
| uk-UA `SettingsUI/Sponsor/Thanks` | 联系作者添加“昵称”和赞助金额 | `ваше ім’я та суму` | 改为 `ваш псевдонім та суму`，同上；调整宾格所有格以配合псевдонім。 |

以上均为局部文案准确性改进，没有发现阻塞性的操作反转／关键警告条件丢失。由根会话协调是否应用，避免与当前验证并发改文件。

## 已核对范围与结论

- 检查四语动作与固定主栏短标签，包括选择／选择清空／穿透、撤销／重做、恢复／定格／锁屏、矩形、不同笔型和白板返回／退出，未发现错误动作含义。
- 逐项核对高风险长说明：配置清理后的丢失/默认值、非空画布保存、更新兼容性与架构、权限匹配与管理员、SuperTop/UAC/安全软件、DDB许可和责任、手掌橡皮/DPI估计/边擦边写、多实例、快捷键模态与AltGr、空历史不传下层、两次Ctrl+Delete和定格不可用行为。条件均保留。
- 对全部679键逐文件扫描与en-US完全相同的长字符串，排除品牌版权／协议名／母语菜单后无意外未译英文长句。剩余同值为正常专名、标识、借词或标点，不作机械漏译判定。
- pl-PL、ro-RO在此次重点语义审查中未发现必须修改的问题。四语“穿透”、笔型、手掌橡皮等自然程度仍按各语言审校记录交给母语者复核。
- 本次没有真实槽尺寸或渲染证据，不因词长猜测缺陷；字号、重音、基线、固定槽和长文裁切由主任务实际离屏／字形测试判断。
