# 翻译完成与审校记录

新增 id-ID、vi-VN、tr-TR、it-IT、pl-PL、uk-UA、es-MX、ro-RO、en-GB，均为679/679个叶子值，共6111个；脚本完成率不等同于母语者认证。

## 地区版本

- en-GB保持en-US术语，必要英式拼写（colour/centred/customise等）；名词licence、动词licensed区分，GNU General Public License专名保留。
- es-MX保持es-ES语义，必要地区词如mouse/computadora，切换失败提示使用完整的clic en el botón。
- 逐key源文/地区差异见 research/regional-changes.json。

## 按实际固定槽测量精炼的26个短标签

保留现有字号与槽宽，完整说明保留；具体测量基线见 research/layout-first-failures.json。

| 语言 | key | 初稿 | 短译 |
| --- | --- | --- | --- |
| id-ID | `UI/Bar/MainButtons/FreezeLabel` | Bekukan | Beku |
| id-ID | `UI/Bar/GeometryAttributes/RectangleLabel` | Persegi pjg. | Psg. pjg. |
| vi-VN | `UI/Bar/Components/RollCallLabel` | Gọi ngẫu nhiên | Chọn tên |
| vi-VN | `UI/Bar/MainButtons/FreezeLabel` | Đóng băng | Giữ |
| vi-VN | `UI/Bar/DrawAttributes/AnnotationLineLabel` | Đường chú thích | Nét ghi chú |
| vi-VN | `UI/Bar/GeometryAttributes/StraightLineLabel` | Đường thẳng | Đường |
| tr-TR | `UI/Bar/DrawAttributes/HardPenLabel` | Sert uçlu kalem | Sert uç |
| tr-TR | `UI/Bar/DrawAttributes/SoftPenLabel` | Yumuşak uçlu kalem | Yumuşak uç |
| tr-TR | `UI/Bar/GeometryAttributes/RectangleLabel` | Dikdörtgen | Dikdört. |
| it-IT | `UI/Bar/Components/ClassIslandSwapLabel` | Cambia lezione | Cambia lez. |
| it-IT | `UI/Bar/MainButtons/ReturnToShowLabel` | Alla presentazione | Alla present. |
| it-IT | `UI/Bar/MainButtons/WhiteboardLabel` | Lavagna | Lav. |
| it-IT | `UI/Bar/DrawAttributes/SoftPenLabel` | Penna morbida | Pennello |
| it-IT | `UI/Bar/DrawAttributes/AnnotationLineLabel` | Linea di annotazione | Linea annot. |
| pl-PL | `UI/Bar/MainButtons/ClickThroughLabel` | Przepuść / kliknięcia | Przep. / kliknięć |
| pl-PL | `UI/Bar/DrawAttributes/LaserLabel` | Wskaźnik laserowy | Laser |
| pl-PL | `UI/Bar/DrawAttributes/HardPenLabel` | Pióro o stałej grubości | Pióro |
| pl-PL | `UI/Bar/DrawAttributes/SoftPenLabel` | Pióro pędzelkowe | Pędzel |
| uk-UA | `UI/Bar/MainButtons/EndPresentationLabel` | Кінець показу | Завершити |
| uk-UA | `UI/Bar/MainButtons/FreezeLabel` | Стоп-кадр | Фікс. |
| uk-UA | `UI/Bar/MainButtons/ClickThroughLabel` | Наскрізь | На- / скрізь |
| uk-UA | `UI/Bar/DrawAttributes/AnnotationLineLabel` | Лінія позначки | Лінія позн. |
| uk-UA | `UI/Bar/GeometryAttributes/RectangleLabel` | Прямокутник | Прямок. |
| ro-RO | `UI/Bar/MainButtons/FreezeLabel` | Înghețare | Îngheț |
| ro-RO | `UI/Bar/DrawAttributes/AnnotationLineLabel` | Linie de marcaj | Linie marcaj |
| ro-RO | `UI/Bar/GeometryAttributes/RectangleLabel` | Dreptunghi | Dreptun. |

## 交叉审查修正

- it窗口追踪使用tracciamento，意大利语/乌克兰语赞助说明明确为昵称；id自动接管说明改为软件自动行为。
- es-MX两条点击按钮语法补en；两组审查记录见research/cross-translation-review*.md。

## 需要母语者审校

- 七种新主语言的穿透、定格、硬/软笔、手掌/笔速橡皮、抬笔笔锋、墨迹预测术语及长权限/安全/数据风险说明。
- 极窄槽中的标准短写尤其id Psg. pjg.、it Lav./Cambia lez.、pl Przep. kliknięć、uk Фікс./Прямок.、ro Dreptun.，确认当地用户易懂；完整说明保持。
- 越南语短译Giữ、Nét ghi chú和土耳其语Sert uç/Yumuşak uç的场景自然度；英/西地区版术语一致性。
- 各语详细审校记录见 research/translation-id-vi-tr.md、translation-it-pl.md、translation-uk-ro.md。

本任务只运行无窗口/离屏验证；未进行GUI实机、触控、其他系统或其他CPU架构验收。

## 离屏图人工检查补正

乌克兰语`SettingsUI/Design/Experimental`侧栏专用译名由“Експериментальні можливості”精简为“Експерименти”，保留Experimental/N等页面完整标题。补全新增九语展开导航实际文字可用宽度检查（旧俄语基线超宽单列记录），并追加乌克兰语页面专项复测；不改全局字体或侧栏几何。
