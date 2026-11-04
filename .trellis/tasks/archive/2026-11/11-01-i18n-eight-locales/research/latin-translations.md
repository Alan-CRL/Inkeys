# 法语、德语、西班牙语翻译交接

## 文件与范围

- `Inkeys/src/i18n/fr-FR.jsonc`：603/603叶子字符串完成。
- `Inkeys/src/i18n/de-DE.jsonc`：603/603叶子字符串完成。
- `Inkeys/src/i18n/es-ES.jsonc`：603/603叶子字符串完成。

以本轮修正后的en-US/zh-CN 603 key为基准，含新增实验室、赞助致谢和调试页的8 key。长说明、错误/权限提示、快捷键规则均已逐项翻译，未仅复制英文骨架，未调用外部翻译服务。

此阶段没有再次修改原三语、快照、生成头、脚本、运行时或其他新语言，也没有运行sync或编译/GUI。三份文件已向主会话声明stable；后续测量若需调整，再按文件归属协调。

## 主要术语

| 概念 | fr-FR | de-DE | es-ES |
|---|---|---|---|
| 画布 | canevas | Zeichenfläche | lienzo |
| 白板 | tableau blanc；短按钮Tableau | Whiteboard；短按钮Tafel | pizarra |
| 撤回 | Annuler | Rückgängig；短按钮Rückg. | Deshacer |
| 重做 | Rétablir | Wiederholen；短按钮Wiederh. | Rehacer |
| 定格 | Figer | Einfrieren | Congelar |
| 穿透 | Passage des clics | Durchklicken | Paso de clics |
| 橡皮擦 | Gomme | Radierer | Borrador |
| 荧光笔 | Surligneur | Textmarker | Resaltador |
| 主栏 | barre principale | Hauptleiste | barra principal |
| 掌擦 | Gomme avec la paume | Handflächenradierer | Borrador con la palma |
| 字体渲染 | rendu | Rendering | renderizado |

- 主栏短标签采用适合现有固定文字槽的紧凑表达，不把长说明直接复制到按钮。返回白板与返回放映分别为 `Au tableau / Diaporama`、`Zur Tafel / Zur Show`、`A pizarra / Al pase`；选择清空与穿透标签可用既有两行例外。
- 颜色通道依本地颜色名称使用缩写：法语R/V/B、德语R/G/B、西班牙语R/V/A；key与数据绑定不变。
- 任务栏缩放说明明确基础量固定48 DIP、实际量随任务栏厚度；掌擦不暗示需要另一支物理笔预先接触，比例只修改预览。权限说明保留核对来源/签名的含义，不加入锁屏能力或关闭防护指引。

## 有意保留的内容

- Inkeys/Inkeys3、GitHub、GitCode、GitHub Discussions、Bilibili、PowerPoint/WPS、SuperTop、PptCOM、Draw3、DesktopDrawpadBlocker、DirectComposition、Zhaoxin等产品或技术名称。
- 既有完整版权署名 `Copyright © 2023-2026 AlanCRL (Chen Runlin / Chen Yunlam) Studio`，以及正式许可名 `GNU General Public License v3.0`；周围说明按语言翻译。
- .NET Framework、COM、UIAccess、UAC、CPU/GPU、DPI/DIP、EDID、Arm64、CI/CD、RELEASE/DEBUG、快捷键组合及格式化token。
- 语言菜单中的母语标签与语言码（English、简体中文、繁體中文等）在三种语言下保持原生名称；Beta依用户规则显示。
- `HeroJoin`保留西文所需空格，句末标点等按拼接合同保留。同形词如法语Options/Animations/Solution、德语Details/Website/Community、以及西语General/Error/Cursor/No，不因与英文拼写相同而强行替换成不自然词。

## 已执行验证

- JSON解析及603个目标key逐项完整性、与基准的key集合/顺序一致，且每条值确实来自完整三语译稿。
- 无待翻译标记、无意外空译文或Unicode替换字符。
- 所有大括号占位符token与基准逐项同序，包含`{}`、`{:.2f}`、`{:.0f}`及四参数实尺说明。
- 换行数与基准一致；仅两个既有短标签按0/1换行例外校验。
- UTF-8无BOM、CRLF保持，无混入LF；构建与真实C++ format/字形/布局测量由主会话集成验证。

最终全目录`check`、603结构生成头/快照sync、ARM64构建及无窗口运行不在本子阶段宣称完成；由主会话统一执行。
