# 交叉翻译审查 2

范围：逐项读取 id-ID、vi-VN、tr-TR 的 679 个叶子，结合当前 zh-CN / en-US 语义；逐 key 比较 en-GB→en-US、es-MX→es-ES 的全部差异。按根会话要求仅审查和记录，没有修改译文、sync、构建、GUI 或测试。

## 具体发现

1. **es-MX 两条切换失败提示：地区化替换缺少介词（已反馈，根会话已修）**
   - `Dialogs/WorkspaceTransitionFailure/Whiteboard`
   - `Dialogs/WorkspaceTransitionFailure/Return`
   - 原文从 `Pulsa de nuevo el botón ...` 改为 `Haz clic de nuevo el botón ...`，`hacer clic` 后需要 `en`。
   - 建议及根会话已确认采用：`Haz clic de nuevo en el botón ...`。不改变重试、取消或返回行为。

2. **id-ID 自动接管说明被写成人工指令（已反馈，根会话已修）**
   - key：`SettingsUI/PlugIn/PPTHelper/Tentative/AutoTakeOverE`
   - 审查时：`Saat anotasi bawaan PPT terdeteksi aktif, beralihlah ke mode menggambar Inkeys dan matikan anotasi bawaan PPT.`
   - `beralihlah` 与 `matikan` 组合读作要求用户手动切换／关闭；中文基准说明的是检测后程序自动进行这两步。
   - 建议明确软件主语：`Saat anotasi bawaan PPT terdeteksi aktif, Inkeys beralih ke mode menggambar dan mematikan anotasi bawaan PPT.`
   - 已反馈根会话并只读确认最终采用上述建议，由其协调资源构建时机；本代理没有修改该文件。

## 其他核对结果

- en-GB 差异限于根身份和英式拼写，`licensed` 的动词形式及官方 `GNU General Public License` 名称保留合理，未发现地区切换改变功能语义。
- 三语选择／穿透、撤销／重做／恢复、自动收起、手掌橡皮识别／阈值及屏幕尺寸回退均保留行为和必要条件。
- 快捷键说明保留“保持 Ctrl、松开后再次按 Delete”“长按不算两次”“新笔迹重置”、AltGr 冲突、模态保留组合、PPT 不接管等条件。
- 插件同权限、UAC 来源核验、失败可能、DDB 限定声明及管理员要求均保留；没有扩写新保证。
- 与英语相同的内容为品牌、协议／架构、RGB／Esc、软件专名、链接、既定版权署名或语言正常借词，未发现未翻译的操作说明段落。
- 短标签的语义可辨：id 的 `Persegi pjg.` 仍指矩形；vi 的 `Bút nét đều / Bút nét mềm`、tr 的 `Sert uç / Yumuşak uç` 与详情说明一致。大小、裁切、字面和读者熟悉度仍需主任务的离屏证据及母语者审校，不在本次纯语义审查中冒称已验证。
