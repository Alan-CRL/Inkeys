# 第二轮验收反馈验证

## 结果

- 三范围直接并入“闲置时自动淡化”同一张卡，移除独立标题/图标；主栏注明仅底栏模式。
- 关闭时内联区按现有180ms条件动画折叠、开启展开；父行和时长下拉常驻，收起/未完全展开时禁交互，偏好保留。卡片高度与内容渐隐同源，支持中途反向和动画关闭。
- 主栏仅本帧BottomDocked生效，包含普通与居中底栏；Floating清进度/期限、回到底栏完整重计时，不清live输入。原外部翻页隔离、悬停和附属面板保护不变。
- 内容上限60%、背景30%，仍按min(normal,cap)只减不增。

## 实际验证

完整 InkeysRepo.sln Debug|ARM64，动态定位ARM64原生MSBuild，同一PowerShell规范化PATH，10分钟预算，最终退出0。未改项目/依赖，警告仅既有HiMsg/hashlib等；日志 Build/ppt-idle-fade-r2/build.*。

| 项目 | 入口/范围 | 退出码 |
| --- | --- | --- |
| i18n | sync/check，20语言，翻译723/723，删除无用scope标题键 | 0 |
| UI | INKEYS_SETTING_PPT_IDLE_ONLY=1，--no-window --setting-home-only，20语言×1/1.5/1.875×340/780DIP，真实Combo/Checkbox、卡片高度、开合/反向/隐藏输入/滚动锚点 | 0 |
| 既有动效 | INKEYS_SETTING_MOTION_ONLY=1，--no-window --setting-home-only，1/1.2/1.875，每组618帧 | 0 |
| 页控/主栏策略 | --page-control-only，新增Floating退出/返回计时/悬停保护 | 0 |
| 生产离屏 | --bar-eraser-offscreen-test，背景30%、内容60%合成alpha约72%、收展/恢复等 | 0 |
| 静态 | 完整增量审阅、git diff --check、原BOM/EOL核对 | 0 |

源文件/产物SHA与逐项退出码在 Build/ppt-idle-fade-r2/source-state.json、validation.json 和 test-results.json。前轮完整--no-window已通过；本轮仅重跑改动相关范围，不把旧全量结果声称为当前版本全量重跑。

## 审阅与调试

- 新内联条件块可能触发视口上方高度补偿；已使SettingRow行末游标继承CursorStartPos重基准delta，未传visibilityId的旧内联行保持旧路径。
- 初版滚动fixture只把行放在视口上方24DIP，展开单帧新增61px导致新范围进入视口，按旧Compensate的max(old,new)规则不应补偿；诊断anchor125/expected64/occupied61确认此原因。只调整fixture余量至200DIP、后续spacer300DIP，并逐帧检查补偿前范围仍在视口外，原锚点≤2px断言未放宽。失败与诊断日志保留attempt-01/02-*，未修改通用补偿策略。
- 审阅报告见check-revision-2.md，产品源码在构建/测试期间冻结；最后两次增量仅测试诊断/摆位。

## 边界

按仓库规则没有GUI/Computer Use/产品或设置窗口测试；未新增跨平台或真实Office/设备观感结论。未commit、未push，任务保持in_progress待用户使用确认。
