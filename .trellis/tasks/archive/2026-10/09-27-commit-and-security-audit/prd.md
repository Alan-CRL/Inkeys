# 提交审查与安全

## Goal

2026-08-10 起相关 commit 逐项覆盖、严重错误和可利用边界修复

## 依赖与门禁

只读风险调查在 baseline-and-acceptance 冻结 H0/commit 集合后即可并行；跨域代码写入须待对应阶段和主 agent 冻结文件 owner，主审与修复收口在 Draw3 阶段后。出口：定义集合逐项 diff 已审、findings 复验与缺口精确，严重确认问题处置后交集成。

## Requirements

- 以 2026-08-10 00:00 +08:00 起、H0 为终点，枚举可获得 refs；逐项审发布线、近期合入的旧日期改动、merge 冲突解决、导入、未合入相关分支，映射等价 patch/revert。
- 每项至少读实际 diff；高风险并发、资源、输入、窗口、PPT、持久化、恢复、依赖深入调用者/被调用者/失败路径及当前实现。
- 严重软件错误与可利用安全边界分别审。覆盖不可信配置/UInk/SVG/图片、路径、更新/TLS/来源、DLL/进程/权限/IPC、日志/dump、依赖官方 advisory；明确可达性与攻击前提。
- 确认问题先有证据/回归，再最小修复与独立验证；不为凑数上报推测，不默认大规模改第三方。
- 安全审查加入目标环境兼容性：Win7 SP1 仅 KB2670838，硬件 FL11.0 有/无、HARDWARE/WARP；正式透明模式只有 DComp/ULW，两个 Win7 DWM 模式均禁用。核 API、导入与失败路径，不把额外补丁当作前提。
- 保留现有 FLIP_SEQUENTIAL；用户已报告该目标环境实测可用。微软文档与目标实测不一致时分别记录来源，不添加 bitblt 回退。

## Acceptance Criteria

- [x] coverage 每行有 SHA、parents、author/committer 时间、分支/H0、模块、类型、当前映射、结论、finding、复验；总/已/待/缺口精确。
- [x] findings 区分 confirmed/hypothesis/already fixed/not applicable/earlier-discovered，并有触发、实际影响、严重性、修复与验证。
- [ ] 无已知未处置的严重崩溃、死锁、数据损坏、输入失效或高危可利用问题；缺失 refs/工具不冒充 PASS。
- [ ] 目标系统的 device、swap effect、presenter 可达性和失败后安全退路有当前代码与官方 API 证据；真 Win7 未运行则保留人工门禁。

2026-10-03收口：前两项以定义的611历史集合及closeout-20261003.md核对为证；后两项受用户保留来源认证风险、真实环境及延期Win7边界限制，不勾为全项通过。续接仅复审新增状态CLI和直接依赖，无新确认安全缺陷。
