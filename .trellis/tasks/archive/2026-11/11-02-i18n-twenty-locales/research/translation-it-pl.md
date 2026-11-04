# it-IT / pl-PL 翻译完成与审校记录

- 两种语言各679/679字符串叶子完成，根Language分别为it-IT/pl-PL；UTF-8、CRLF、原JSONC两空格/花括号布局与注释头保持。
- 以zh-CN逐项语义为准、en-US对照；对撤销/重做、清空与选择清空、穿透、定格、手掌橡皮、放映接管、崩溃/重启/权限、快捷键长说明保留完整条件。
- 默认按钮：it OK/Annulla/Sì/No；pl OK/Anuluj/Tak/Nie，与消息框分派约定一致。
- 意大利语屏幕定格采用Fermo immagine/主栏Ferma，与系统锁屏Blocca schermo/Blocca PC区别；波兰语分别为Zamroź ekran与Zablokuj ekran。
- 新的通用品牌为Inkeys；GitHub社区分流参考en-US而非复制中国QQ群号码。DDB中国软件专名、协议/链接/键名和母语语言菜单保留原样。
- 主栏短词按语义精炼，例如it Penna/Pennello/Impost.、pl Pióro/Pędzel；全称及解释在对应说明中保留。没有增加换行例外或修改旧文件。

## 已完成验证

Python标准库解析JSONC并独立比较：完整679键集合和顺序、占位符字面与顺序、必要换行（只有既有SelectClear/ClickThrough允许0/1）、NFC、无空值/待译标记均通过。与en-US相同的值已逐项审阅，仅正常借词、技术标识、品牌、母语名称、链接与标点等有意同值。未运行全局sync、构建或GUI，由根会话统一验证。

## 母语与视觉审校重点

- it：Clic passanti作为穿透模式名称、Fermo immagine/Recupera的教学批注语境自然程度；Penna dura/morbida及手掌/速度橡皮术语。
- pl：Przepuszczanie kliknięć、Gumka dłonią、Gumka zależna od prędkości及Pióro pędzelkowe的术语习惯。
- 两语：带安全与数据风险条件的长说明（SuperTop、历史画布、快捷键）需母语者复核措辞。已按中文完整翻译，不宣称完成母语人工认证。
- 长标签可能由现有生产拟合处理；应由本次20语真实字形/页面离屏测试确认字面比例、重音与排版，不以结构通过代替视觉验收。
