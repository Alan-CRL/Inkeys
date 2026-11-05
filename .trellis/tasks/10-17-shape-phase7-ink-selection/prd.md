# Phase 7 墨迹统一编辑

套索、多选、墨迹变换、组合及发布兼容回归；依赖 Phase3/6。

依赖与边界见父任务。状态：planning；本轮不实施，正式开始前补齐design/implement与回归验收。

## 2026-11-05 研究资料（正式设计前必读）

- `research/user-decisions-and-notes.md`：用户对合成缓存调研的纠正、已定的前置工程项（多对象排除、缓存预算可配置与无缓存降级、generation 全局唯一），以及尚未定案的橡皮语义问题。
- `research/composition-cache-mid-z-move.md`：代码调研，涉及合成缓存的现状、在 Z 序中间移动对象所缺的能力，以及方案 A/B/C 的比较。
- `research/lasso-transform-survey.md`：用户提供的外部技术调查原文，包括编辑对象、局部擦除蒙版、延迟生成 Fragment，以及 P0–P3 分期。
