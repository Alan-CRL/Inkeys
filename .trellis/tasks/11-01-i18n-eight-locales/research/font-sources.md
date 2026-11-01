# Noto 字体来源候选（规划调查）

核验日期：2026-11-01。尚未下载字体、修改 RC 或选定字体文件。

- Google Fonts 官方目录直接提供用户要求的家族：
  - https://github.com/google/fonts/tree/main/ofl/notosansjp ，含 `NotoSansJP[wght].ttf`、`METADATA.pb`、`OFL.txt`。
  - https://github.com/google/fonts/tree/main/ofl/notosanskr ，含 `NotoSansKR[wght].ttf`、`METADATA.pb`、`OFL.txt`。
- Noto 官方部署说明：https://github.com/notofonts/noto-cjk/blob/main/Sans/README.md 。该说明区分 variable、OTF、区域子集和集合字体；不应把同为日/韩字体的各部署格式视为可无条件互换。

## 实施前技术核验

1. 优先保持用户点名的 Noto Sans JP / KR 家族；固定上游 commit 与文件哈希，保留对应 OFL，不依赖运行时网络。
2. 当前项目同时有 DWrite 与 ImGui 字体消费者；验证二者对候选文件与字重的实际支持，以及现有 Windows 兼容边界。必要时采用经验证的静态实例，记录可复现来源/生成步骤，不升级工具链或改变平台范围。
3. RC 嵌入之外，检查解包路径、缓存更新、字体族内部名称、语言页多语名称字形与动态重建；不能以 RC 存在替代实际加载验证。
4. 资源大小、字重选择和字形覆盖在候选文件检查后记录；此刻不宣称已完成体积或兼容验证。
