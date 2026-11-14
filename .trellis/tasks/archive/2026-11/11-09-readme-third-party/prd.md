# README 项目引用与 NOTICE 修订

父任务：`11-09-readme-docs-refresh`

## Goal

修订 README「项目引用」：新增「使用方式」一列，以 dev 为准；标出已不再使用的组件，但保留在表中；补齐缺失的组件；改正不准确的版权声明。

## 已确认决策（2026-11-09）

- 收录范围：只收代码库，包括 hashlib++ / Zip Utils 里内嵌的子组件。**不收 vcpkg 的传递依赖**（Brotli、fmt、PlutoVG）：用户决定 README 和 NOTICE 都不列（2026-11-09）。核实情况：Brotli 未链接进程序（没有定义 CPPHTTPLIB_BROTLI_SUPPORT）；fmt 的许可证允许以二进制形式分发时不附声明；PlutoVG 确实编进了程序，MIT 严格来说要求署名，已向用户说明。不收字体、图片、NuGet 包、构建工具、测试工具。
- 状态列只用一列「使用方式」，取值：vcpkg / 源码内置 / Git 子模块 / 随程序分发 / 已不再使用。
- 版权声明一律逐字摘自许可证文件；许可证里没有版权行的，就写「许可证文本无版权行」或「未随附许可证文件」。

## 调查

- Codex `gpt-6.1-sol` High（service_tier=default，只读沙箱，约 14 分钟，exit 0，工作区无改动）。
  - 简报：`research/codex-brief.md`；报告：`research/codex-third-party-inventory.md`。
  - Codex 沙箱里不能运行 git，事先导出的历史材料在 `%TEMP%/inkeys-codex/third-party/`。
- Claude 独立核对，全部属实：
  - NOTICE 里 LunaSVG 写成了 JsonCpp 的版权。
  - ImGui 版权 2014-2026（1.92.7）。
  - HiEasyX：demo 删于 `b91366d6`，主程序删于 `41535eaf`。
  - `IdtFloating.cpp` 是 `<None>` 项，不参与编译。
  - imgui_toggle 和 DirectX SDK 不在编译项里。
  - msgpack 经 `uink_codec*.cpp` 编进主工程。
  - vcpkg 各 port 的 `copyright` 和 `Vcpkg/ports/*/vcpkg.json` 的 license 字段逐个核对过。
- 补查：
  - HiEasyX 历史上没有随附 LICENSE；版权按上游 LICENSE 填（MIT，`Copyright (c) 2022 zouhuidong`）。
  - imgui_toggle 上游是 cmdwtf/imgui_toggle，许可 0BSD。

## 实施记录

- `README.md`「项目引用」已改写，CRLF / UTF-8 保持不变。
  - 合并成一张表，删掉原来单独的「第三方组件」（EasyX）小节。
  - NOTICE / ThirdpartyLicenses 改用相对链接，不再固定指向 main。
  - 「本使用条款」改为「本项目的许可条款」。
- 未纳入：ICU，用户确认从未用过。
- OpenCV：用户要求收录。`accf202b` 2026-08-20 经 vcpkg 接入 `opencv4 4.10.0#3`，未合入 dev，标为「已不再使用」。上游 LICENSE 里没有版权行，版权逐字摘自上游 4.10.0 的 COPYRIGHT 文件。
- NOTICE 已重写（2026-11-09）：
  - 32 个组件（去掉 3 个传递依赖后），与 README 表一一对应，脚本核对过。
  - 字段为 License(SPDX) / Copyright（逐字）/ Usage / Notes。
  - 修正了 LunaSVG 误用 JsonCpp 版权、Abseil 误写 LunaSVG 版权的错误。
  - FreeType 的 FTL 署名语句保留在 Notes 中。
  - CRLF、UTF-8 无 BOM、结尾换行保持原样。
- 不修改 TOS（用户明确要求）。

## 待办

- [x] NOTICE 同步
- [x] ThirdpartyLicenses：`hashlibpp License` 已按源码改为 `Grüdelbach`（仅改这一处，按字节替换）。
- [ ] 中文定稿后翻译 README_EN 同节。
- 风险提示（不在本任务范围）：Segoe Fluent Icons、HarmonyOS Sans、Office PIA 等随附资源的授权依据未取得，见 Codex 报告第 3 节。
