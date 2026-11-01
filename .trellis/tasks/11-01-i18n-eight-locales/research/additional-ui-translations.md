# 活动组件与DDB详情新增文案交接

## 变更范围

依据主会话提供的三份准确清单，为以下六份JSONC各补76个key，由603扩展为679叶子：zh-CN、en-US、zh-TW、fr-FR、de-DE、es-ES。

| 清单 | 数量 | 落点 |
|---|---:|---|
| `component-keys.json` | 37 | `SettingsUI/Component`完整名称/分类/协议条件27条；`UI/Bar/Components`独立主栏短标签10条 |
| `ddb-keys.json` | 38 | `SettingsUI/PlugIn/DesktopDrawpadBlocker`许可、启用、间隔、识别对象及条件说明 |
| `misc-ui-keys.json` | 1 | `SettingsUI/DebugSoftware/DisplayInformation` |

新节点按已有父对象自然相邻插入，保留每种语言原603项的实际值及格式，不重排整个文件。特别核验主会话修正的西班牙语CombinedDescription仍为：

> Usa Selección para manejar el ordenador tanto sin trazos como manteniéndolos visibles.

原三语新增76个key已全部写入`existing-locales-reasons.json`（slash路径）；旧原因映射保持，最终前后对照报告可完整列出新增项。

## 文字处理

- 设置页的完整操作名称、软件分类与主栏短按钮文案分离；不把长标题塞入70 DIP固定按钮。
- 区分键盘模拟、随机点名及ClassIsland联动；保留IslandCaller/SecRandom/NamePicker/ClassIsland专名、版本、兼容模式及URL/IPC注册要求。没有课表时不生效的条件没有删除。
- 繁中采用檔案總管、工作管理員、控制台、外掛程式、設定檔、浮動視窗等当地表述。
- 法语主栏使用Fichiers/Tâches/Panneau/Bureau/Verrou/Échap/Tirage/Régl. CI/Profils/Changer等短词；德语使用Dateien/Prozesse/System/Desktop/Sperren/Esc/Auslosen/CI-Einst./Profile/Tauschen；西语使用Archivos/Tareas/Panel/Escritorio/Bloquear/Esc/Sortear/Ajustes CI/Perfiles/Cambiar。最终是否超出文字槽由真实DWrite测量决定，此阶段没有声称已测过宽度。
- 国内软件名保持原产品名，如希沃白板3、希沃轻白板（5C）、希沃品课教师端、希沃PPT小工具、欧帝白板、天喻教育云互动课堂、畅言智慧课堂、C30智能教学和希沃桌面；只翻译浮动视窗/侧栏/PPT工具等用途描述。不猜测官方英文品牌，也不把显示字符串用于窗口匹配。
- DDB许可说明完整保留原有“仅限学习交流研究、禁止其他用途”、GPLv3发布、仓库URL、不提供质量保证、用户承担后果及使用视为同意的声明；五行结构不变。不借翻译调整许可条件。
- DDB详情保留管理员条件、白板模式自动恢复、窗口追踪、PPT控件包含关系及1.0/2.0/2.5/3.0普教/高教版范围，不改变功能或兼容声明。

## 校验结果

- 六语均679 key，JSON正常解析、key集合与顺序一致。
- 每个新key均完成翻译；没有空译文、待翻译标记或Unicode替换字符。
- 全679项的大括号format token严格同序；换行数与基准一致，仅保留原有两个主栏短标签0/1换行例外。
- 原有603值逐项保留，包含主会话对西班牙语说明的修改。
- 原三语所有实际新增或改动key均有原因映射。
- UTF-8无BOM、CRLF检查及相关文件`git diff --check`通过。

本阶段不运行sync、不改生成头/快照/C++、不编译、不运行GUI。其余五语、679结构的最终sync与全目录check、实际format/字体/布局和构建验证由主会话及相应实现者集成。
