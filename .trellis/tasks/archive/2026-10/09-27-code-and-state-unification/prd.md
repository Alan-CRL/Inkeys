# 代码规范与统一入口

## Goal

第一方代码规范、职责与状态写入者审查、统一语义入口与回归

## 依赖与门禁

入口：依赖 baseline-and-acceptance 冻结 H0、回归表和 UI3/Draw3 可测基线；严重缺陷调查可先读不写。出口：状态/入口迁移表、相关生产逻辑测试、独立 review 与完整 Solution 构建完成，才允许 UI3 写入阶段。

## Requirements

- 仅审第一方当前编译代码；建职责图及状态 authoritative/requested/applied 写入者、读取者、线程、副作用清单。
- 核对现有 ChangeStateModeTo*、SetPenWidth/SetPenColor、SyncDraw3State、ReconcileDraw3Presentation、StateModeTransitionRevision 语义；按钮、快捷键、PPT、设置恢复、自动切换等同意图走同一语义入口。
- 只按职责拆必要 coordinator、状态/命令、layout、animation、render resources、lighting、interaction；保留 C++20 modules、ODR、ABI、原编码/换行、现有 UI/Draw3 所有权。关键逻辑添加适量中文注释。
- 同层 UI 公共能力按需要复用，保留 ImGui 设置、Bar、专用动画、Draw3 差异；不强迁、不引入第三套框架。

## Acceptance Criteria

- [x] 有功能→旧入口→规范入口→迁移调用点→保留例外的迁移表和状态所有权清单。
- [x] 跨入口等价、重复调用、快速切换、迟到回调和取消通过生产逻辑测试；正常 UI/光标/输入/窗口副作用保持一致。
- [x] 最小 diff、无无关格式变更；独立审查、完整 Solution 构建与适用测试记录。

2026-10-04：第二项确定性状态/bridge与FineDial交错已有两配置CLI通过，正常GUI/Office副作用按用户人工确认通过；见closeout-20261003.md最上节。未外推为FineDial成功保存逐例、真实驱动故障、量化性能或Win7验收。用户要求任务继续，不据勾选归档或结束首发任务。
