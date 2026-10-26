# 实施与验证记录

2026-10-10，在现有工作区和 fluent 分支实施；子任务已挂 09-06-setting-winui3-design-rebuild。trellis-implement 实现，trellis-check 独立审查并修复，主会话串行拥有完整构建和测试产物。未启动 GUI/Computer Use/浏览器，未 commit/push 或修改远程内容。

## 实施结果

- 阅读顺序为欢迎语 → 居中网站彩虹手写字标和标语 → 完整版权/许可证 → 开源名片 → 原探索及其余区域。保留原书写/教程行为及其余功能布局。
- NewHome3 六组曲线、0.14 右倾、速度压感、弧长彩虹与浅深配色提取为本地矢量缓存；沿用 ImDrawList，无新设备/纹理，每帧仅缩放/平移。完整 y 下伸、8 DIP 圆角、28/20 DIP 内边距、80%/520 DIP 预算。
- 真实三语言全文，包括双英文署名 Chen Runlin / Chen Yunlam；GPL 全名以及 GitHub/GitCode 是各自可换行的名称链接。开源卡 Star/心形 Sponsor 复用 ImFluent 和原动作，按真实宽度下移/换行。
- HomeAction 末尾追加 License/GitCode，既有值保留；目标链接在 Setting.cpp 原异步队列宏域处理，Star/Support 复用已有路由。绘制不发起外链。
- i18n 源修改后 sync 生成 header/快照并 check；只登记必要新头，不改网站/vendor/工具链/依赖。关键布局和缓存用适量中文注释。

## 最终验证

| 验证 | 结果与证据 |
| --- | --- |
| 完整 Solution | PASS，InkeysRepo.sln Debug\|ARM64；ARM64 原生 MSBuild，动态 vswhere 定位，同 PowerShell invocation PATH 规范化和 MSBUILDDISABLENODEREUSE=1；最终 ExitCode 0，83.289 秒 |
| 无窗口完整回归 | PASS，Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window；ExitCode 0，182.212 秒 |
| i18n check | PASS，en-US / zh-TW 各 383/383（100%）；新文案均在源文件，生成物同步 |
| 编码 / BOM / 换行 | PASS，tracked 对照 git cat-file --filters HEAD checkout，保持原 UTF-8/BOM/CRLF/末尾换行；新任务和头文件 CRLF；最终审计在所有记录写完后执行 |
| diff | PASS，git -c diff.ignoreSubmodules=all diff --check；最终范围审查只包含本任务产品、测试、生成物和 Trellis 记录 |

构建最终 0 error、9 个既有 HiMsg/hashlib++ 第三方 warning；本次新增的通道/测试数字转换 warning 已修正，未清理无关警告。日志保存在忽略目录 Inkeys/Cache/home-design-20261010：build-final-tests.log、build-final-tests-result.json、headless-final.stdout/.stderr、headless-final-result.json、i18n-check-final.log、encoding-audit-final.json。每个构建/测试设置 1800 秒 deadline，检查实际退出码。

生产 RenderHome/真实字体/实际 ImGui 输入和 DrawData 覆盖三语言、浅深、导航展开/收起，以及 720×520、960×700、1440×900 对应内容预算；补充 320×520、480×520 约束场景。沿用并补充倍率 1 / 1.25 / 1.5 / 1.875 / 2 / 3 / 4，共 420 个布局组合。检查完整字标比例/下伸、英文署名、文本与按钮预算、卡片增高、无横向溢出、外层滚动和字体/style 栈恢复。

对实际 DrawData 定位的每个 GPL 名称片段、GitHub/GitCode 和两个开源按钮，验证轻点正确动作一次、空闲不重复、真实 Tab 进入键盘导航后 Enter/Space 一次激活、焦点可见、内容拖动转移 capture 并取消动作；至少一个实际字体宽度强制的多片段许可证场景。所有动作仅观察返回值，不执行外链。保留并通过原主页滚动/控件和完整 headless 其他回归。

## 首轮失败的诊断与修复

- restricted runner 构建遇到 C1902 Program database manager mismatch，涉及既有文件/PDB，为本机 Codex 沙箱构建环境问题。按相同 solution/configuration 在普通用户环境验证后消失；未更改源码/SDK/工具链绕过。
- 随后真实源码错误为新增调用 ImFluent::GetColorU32 缺内部声明；按现有 Setting.Shell.cpp 约定在 Pages.cpp 直接 include imfluent_internal.h，完整构建通过。独立审查还去除 InvisibleButton 已绘制的重复导航焦点框。
- 首轮完整 headless ExitCode 1，164.723 秒，193 项新测试失败：鼠标轻点后 NavCursorVisible 隐藏，fixture 未真实进入键盘模式即发 Enter/Space（96+96）；另一个用 320 DIP 假设每种字体都分行。均为新增测试条件错误。依据 ImGui 实际门槛补真实 Tab 输入和严格无动作/同 ID/可见焦点断言；根据真实 Caption 字体宽度判断必分行，保留所有片段激活和至少一个多片段要求。产品布局和路由没有因此修改，最终完整重编/重测通过。

## 独立审查与规范

独立审查见 research/check-review.md；规范更新审计见 research/spec-review.md。rendering-and-ui.md 只追加首页矢量、布局、链接、输入与验证合同。原生 HWND/DX11 最终观感及实机触摸尚未执行，CPU 测试不替代人工实机验收；按用户授权保留为后续人工审阅。任务保持 in_progress，不归档或提交。
