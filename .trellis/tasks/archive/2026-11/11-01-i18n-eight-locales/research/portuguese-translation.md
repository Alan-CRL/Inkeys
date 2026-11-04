# pt-BR 翻译实施记录

## 完成内容

新增 `Inkeys/src/i18n/pt-BR.jsonc`，完整翻译当前603个叶子字符串，包括新增实验室、赞助说明和UI3渲染诊断条目。以当前修正后的zh-CN/en-US语义为准，未执行共享sync，未修改原三语、CJK或其他语言资源。

## 巴西葡萄牙语用词

| 概念 | pt-BR |
| --- | --- |
| 设置／界面选项 | Opções |
| 白板 | Lousa |
| 画布 | Tela de desenho |
| 画笔／橡皮擦 | Caneta / Borracha |
| 荧光笔 | Marca-texto；主栏短动作为 Realçar |
| 选择 | Selecionar |
| 穿透 | Passagem de cliques；主栏两行为 Passar / cliques |
| 定格 | Congelar |
| 撤回／重做 | Desfazer / Refazer |
| 插件 | Complementos |
| 笔触觉反馈 | Resposta tátil da caneta |
| 任务栏／快捷方式／用户 | Barra de tarefas / Atalho / Usuário |
| 帧率 | Taxa de quadros |

主栏使用紧凑动作词，并用「À lousa」与「Ao slide」区分返回目标。硬笔／软笔短标签为「Rígida／Suave」，绘制面板中使用完整「Caneta rígida／Caneta suave」。选择清空及穿透短标签使用允许的双行格式；其他资源没有自行增加换行。

所有长说明均完成葡语翻译，没有英语填充段落。输入模式、按住锁定、掌擦接触识别与预览比例、48 DIP固定基准／任务栏实际厚度、全局快捷键保留和两次Delete序列等条件均按已核实语义保留。UIAccess说明不保证锁屏能力，UAC使用巴西Windows惯用的「fornecedor」（发布者）一词；保留五行警示结构。

RGB字母、平台与产品名、授权全名、Arm64、母语语言标签、英文姓名署名和必要标点不翻译。Laser、Mouse、hardware、software、console以及开发信息中的branch、commit是巴西技术语境常用词，保留这种写法；相关说明本身均为完整葡语。format说明符仍为原有 `{:.2f}`、`{:.0f}`、`{}`，未将格式语法的小数点改为逗号。

## 已执行检查

- 对照当前zh-CN：603/603个key及顺序一致。
- 格式token逐项同序、普通换行数一致；两个短标签至多一个换行。
- 非空基准没有空译文，无待翻译标记，Language为pt-BR。
- 严格UTF-8解析、无BOM、CRLF检查通过。
- Recall未引入百分号，含时间戳的固定缓冲边界检查通过。
- 与英语相同的值只包含上述专名、常用技术词、母语标签及必要标点／空白。
- `git diff --check --ignore-submodules` 通过。

尚未执行构建、真实C++ format、DWrite／ImGui字形或离屏布局测试，交由主会话统一验证。未运行GUI。
