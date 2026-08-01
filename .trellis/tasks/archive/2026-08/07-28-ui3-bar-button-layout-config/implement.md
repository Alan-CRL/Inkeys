# 实施计划（草案，待审阅后执行）

> 研究/决策已收敛。用户批准最终规划前禁止改产品代码。

## 已定决策摘要

1. 三数组：`FixedButtonsA1` / `ExtensionButtons` / `FixedButtonsA2`
2. 元素：`{Id, Size}`；B 另有 `Visible`；A 无配置 Visible
3. A 严校验：required Id 多重集合恰好排列，否则该区重置默认
4. 升级新增官方按钮 → 缺项 → A 区整区重置
5. Size 本轮=注册默认镜像；缺省/非法/非默认纠正写回；不因 Size 重置 A
6. A 误带 Visible → 忽略剥离
7. B 未知插件 ID 永久保留；不 GC
8. 旧 `ButtonLayout` 拆分迁移后再走上述规则

## 实施清单

1. 定稿字段名与 JSON 枚举字符串（Size 四值命名与 `BarButtomSizeEnum` 映射）。
2. 扩展 `Other.Config` schema：三序列 + codec；A 写回剥离 Visible；Size 纠正策略。
3. 旧 `UI.Bar.ButtonLayout` 读兼容迁移 → 拆 A1/B/A2 → 校验/纠正 → 写新字段。
4. 按钮注册表增加 zone/required/defaultSize/defaultUserVisible。
5. 重写 `BarButtomSetClass::Load()`：normalize A1 + B + A2，合成 `buttomlist`，写回配置。
6. Geometry 等 A 区默认隐藏改为注册写死 `userVisible`，不读 A 配置 Visible。
7. 更新 `.trellis/spec/native-desktop/configuration-i18n-and-assets.md` 合同与错误矩阵。
8. 静态场景表：缺项重置、错区重置、升级加按钮重置、Size 纠正、Visible 剥离、未知插件保留、旧数组迁移。
9. `git diff --check`；`InkeysRepo.sln` `Debug|ARM64` 构建。
10. `trellis-check` 全量复核。

## 验证矩阵（实施时）

| 场景 | 期望 |
| --- | --- |
| A1 缺 Geometry | A1 整区默认，B/A2 不动 |
| A1 含 Pierce | A1 重置默认 |
| A1 元素带 Visible | 忽略剥离，顺序保留（若 Id 合法） |
| Size 非法 | 纠正注册默认，不重置 A |
| B 未知插件 | 保留不渲染 |
| B 含 Select | 剔除 |
| 旧 ButtonLayout | 拆分后 A 严校验、B 保留扩展项 |
| 新版本 A 多一个官方按钮 | 旧 A 重置默认 |
