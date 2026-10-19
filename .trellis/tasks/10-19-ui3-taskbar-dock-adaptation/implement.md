# 实施计划（已实现并通过自动验证）

## 进入实施前
- [x] 完成主栏PPT规则及双画布Desktop/PPT/Whiteboard范围确认。
- [x] 将复杂需求拆成两个可独立验证的子任务，各备PRD/design/implement与真实context entries。
- [x] 用户批准最终规划，父任务和两个子任务已运行task.py start。
- [x] 实施代理读取子任务规划/manifest并补读相关spec；主会话协调，无递归派生实现/检查代理。
- [x] 已记录/复核原编码、BOM、CRLF与dirty范围，关键步骤中文注释；本任务无commit授权。

## 执行顺序与责任
1. 10-19-canvas-edge-fullscreen-policy：Window Service集中应用双画布region/NonRude策略，补启动/白板进退/回滚/重建一致性及相关无窗口用例。保持Draw3尺寸/笔迹算法不变。
2. 10-19-taskbar-dock-follow：Display采样和observer、统一Bar目标线、Y过渡/输入交接、Bar最终viewport与直移底行保护；补现有Bar/Display/启动预览相关用例。
3. 父任务整合审阅：确认两种1px策略范围不同，画布策略不依赖Shell显隐，Bar与PPT优先级不被画布规则覆盖；全范围Trellis check并记录实际spec决策。

两个子任务没有功能依赖，按上述顺序减少共享窗口环境交接风险。父任务不复制产品实现，不以完成一个子任务代表需求整体完成。

## 自动验收
- 主栏几何：四edge、autoHide开关、Hidden/Raised/Unknown、多屏归属/负坐标、真实DPI/UI倍率、普通/居中/浮动；边框5DIP及任务栏稳定top准确。
- 观察器：burst合并、重复快照不发generation、旧HWND/epoch忽略、Explorer重建/失败恢复、hook/timer同线程清理；快速路径不枚举EDID/所有窗口。
- 动画与输入：重复目标幂等、从当前Y反向、动画关闭、X/居中不变、拖动/Absorbing/恢复单写者、旧帧屏障；PPT/白板优先，退出采用最新桌面目标。
- Bar底行：viewport bottom<=monitor.bottom-1，覆盖描边/光影/峰值/UI倍率；跨屏/模式变化清旧像素，SetWindowPos直移不越界。
- 双画布：Desktop/PPT不论任务栏位置/autoHide都排除1物理行并应用NonRude；Whiteboard整屏且允许既有全屏；两个角色一致，selection固定穿透不变。
- 双画布生命周期：创建前Show、scene/bounds/重建代际、相同请求零重复调用、单窗/双窗失败回滚与重试、白板取消及Exiting重入；client/Host/RTS/doc尺寸不变，无策略切换引起的resizeCount增加。
- 测试直接消费生产helper/事务接口；优先扩展InkeysHeadlessTests现有Bar/Display/PPT/启动预览与Window相关入口，只有必要时新增测试源及注册。

## 构建与执行
- 批准实施并完成代码后，复用InkeysRepo.sln Debug|ARM64；vswhere定位ARM64原生MSBuild，PreferredToolArchitecture=arm64，同一PowerShell规范化PATH并设置MSBUILDDISABLENODEREUSE=1；timeout至少5分钟，建议900秒。
- 上一任务的沙箱MSBuild节点通信问题如再出现，先判定环境再按权限机制处理，不改工程/工具链绕过；不反复启动同一挂起构建。
- 运行新产物InkeysHeadlessTests.exe --no-window及有必要的既有CLI回归；TEMP/TMP可用仓库scratch避免sandbox临时目录原子替换权限问题。
- 原任务不新增配置/i18n；追加调整涉及既有开关默认值，须执行真实配置边界验证，不新增i18n字段。检查退出码与首个有意义错误，在本任务research/validation.md记录代码与环境结论。
- 无窗口测试与编译不证明实际Shell自动隐藏、分类和动画视觉效果。真实DComp/ULW、Win7、UIAccess、多DPI/多屏以及鼠标/笔触发需用户另行授权GUI后验收，不能写成已通过。

## 回退与结束
保留上一已提交缩放功能，仅回退本任务负责变更。observer停用先失效回调再卸载hook/timer；画布退出/销毁恢复本任务region/property，不留下半套策略。完成后报告实现、自动验证、未实测限制；未经本任务新的明确授权不commit、不push。

## Context校验提示
2026-10-19 task.py validate通过。rendering-and-ui.md、draw3-integration.md、runtime-and-rendering.md部分超出32768字节注入上限；注入内容会截断，不等于已经完整阅读。实施及check代理须按本任务研究中的章节/源码锚点，用rg定位并显式补读相关spec全文段落（白板/双画布/窗口所有权/resize/Bar贴底与输入呈现），不能仅依赖manifest自动注入。无需拆分或改写既有spec来消除该警告。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。

## 执行完成记录
2026-10-19：所列实现/静态检查/完整solution及无窗口验证已完成，详见父任务research/validation.md。涉及真实Shell/窗口视觉的项目仍未经GUI验证；此处勾选实施步骤不代表已取得实机GUI结果。未commit/push，任务保留in_progress待用户验收。

## 用户追加调整（2026-10-19，已授权直接实施）
1. 明确顶部/左侧/右侧任务栏对应的桌面主栏贴底线为monitor.bottom-round(5*真实DPI/96)，普通/居中底栏共用；底部autoHide Hidden和PPT原5DIP规则保留，普通底部/autoHide Raised仍贴任务栏top。
2. UI.Bar.UseTaskbarHeightAsScaleBase在缺省时仅设备“默认分类”为大屏时默认true，其余false；采用现有默认设备判定，而不是用户在deploy.json中覆盖的PaintDevice。明确保存的true/false仍优先。构造/ReadMini/ReadAll/Reset默认规则一致，保持NoUpload/严格bool及Zoom2语义。
3. “未来PC UI后再都默认开”记录为后续方向，本轮不实现PC UI或新增样式开关。启动默认判定的采样、缓存与Mini时序需由研究确认最小接线，再交实现代理；不得以早期无完整显示快照而让Mini与正式默认互相矛盾。
4. 继续最小diff、原BOM/CRLF、无GUI、无commit/push；完成后完整solution Debug|ARM64及适用无窗口/配置边界验证。此前validation为追加调整前结果，不能当作本轮新修改验证。

## 追加调整自动验证完成
2026-10-19：顶部/左右5真实DIP与设备分类条件默认已实现并经trellis-check审阅。新完整solution Debug|ARM64 exit0（130.11秒、0errors/183既有warnings，无warning落在本轮修改行）；新Headless --no-window exit0（106.19秒）；新产品 --config-boundary-test exit0（2.65秒）。编码/CRLF及diff检查通过。细节见research/followup-validation.md。未GUI、未commit/push，保留in_progress待用户实机验收。

## 提交授权
2026-10-19：用户明确要求“为改动提交commit”，本任务源码、测试、规范及任务记录按一个工作提交处理，沿用现有SSH签名配置，不push。此前未提交/未获提交授权的表述为验证当时状态；用户本次未要求归档，父子任务暂保留in_progress。消息钩子与定时兜底说明已按Display.cpp现有实现核对，不新增性能优化或修改检测频率。
