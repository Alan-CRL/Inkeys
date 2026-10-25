# 标题栏版本色与汉堡反馈验证（2026-10-09）

## 变更与范围

- `Setting.cpp` 仅把实际标题栏版本文字从 TextSecondary 改为 TextPrimary，与邻近主题/Debug/caption图标一致；活动1.0、非活动0.55系数、命中及字号保持。
- `Setting.Shell.cpp` 仅对 `##pane-toggle` 局部映射 ControlFillSecondary/Tertiary 到当前palette的 subtleHover/subtlePressed，style push/pop配对。透明默认态、无边框、实际Fluent Button ID/颜色动画/焦点/按下释放/toggle保留，不修改vendor或全局按钮。
- `setting_design_tests.cpp` 在现有真实ImGui导航回归中读取实际DrawData背景、菜单白像素顶点alpha；验证浅深、展开/紧凑/覆盖、1/1.5/1.875倍率中的默认透明、hover、held、释放一次切换和移出透明，不增加产品接口。
- 仅本轮源码/测试与本记录由implement代理修改；规范、父任务metadata与journal由主会话维护。前轮调试任务归档未修改，本轮没有commit/push或GUI/Computer Use。

## 最终结果

日志均位于 `Inkeys/Cache/titlebar-menu-feedback-20261009/`，没有覆盖上轮debug证据。

| 检查 | 结果与证据 |
| --- | --- |
| 完整Solution | `build-accepted-result.json`：退出0，23.6967784秒；`build-accepted.log/.stdout/.stderr`。动态vswhere定位ARM64原生MSBuild，在同PowerShell调用规范化PATH并设置MSBUILDDISABLENODEREUSE=1，完整InkeysRepo.sln Debug/ARM64、/m:1 /p:CL_MPCount=1、1800秒deadline。最终9条既有HiMsg/hashlib第三方warning，无新增本轮错误。 |
| 全部无窗口测试 | `headless-accepted-result.json`：退出0，120.0476934秒，stderr 0字节；`headless-accepted.stdout`最终PASS animation correctness。真实产品控制测试随完整 `InkeysHeadlessTests.exe --no-window` 运行。 |
| 汉堡回归 | 三倍率各546连续帧，18个浅深/模式场景全部通过；held必须严格比先前hover更接近pressed目标，避免容差重叠把无变化误判通过；释放不改变页面/业务事件，长按不反复toggle。 |
| 字体/i18n | 原字体资源与atlas沿用；`i18n-check.log`：Scripts/i18n.ps1 check退出0，en-US与zh-TW均370/370。 |
| 编码/差异 | `encoding-check.json`：三源码/测试UTF-8、原无BOM、纯CRLF保持；Git对象中的LF由autocrlf规范化；主会话使用git cat-file --filters取得checkout基线，与全部10个tracked改动比对UTF-8/BOM/CRLF通过，见final-format-check.json。git diff --check --ignore-submodules退出0，无无关生成文件差异；task validate亦通过。 |

实际最终输出：`D:\Project\Inkeys\Repo\Inkeys\Build\ARM64\Debug\Inkeys.exe`、同目录`InkeysHeadlessTests.exe`。版本按钮色通过实际生产路径静态核对，相邻图标均使用TextPrimary及相同active/inactive系数。

## 实际颜色与测试计量修正

Fluent已有 AnimateColorU32 使用83ms渐近混色、8位存储。不得把数学无限精度端点当成真实8位绘制结果；仅本轮新增稳定颜色检查允许最多3LSB误差，其他既有回归不改。

| 主题 | hover可见灰度 | held可见灰度 | 30帧移出后可见灰度/实际fill alpha |
| --- | --- | --- | --- |
| 浅色（目标hover231/pressed224，底色243） | 231.235 | 226.133 | 242.875 / 2 |
| 深色（目标hover53/pressed47，底色32） | 52.5882–53.5686 | 48.8667 | 32.1412 / 2 |

held实际始终有颜色变化且更接近pressed。初始idle和移出稳定alpha均<=3；30帧移出等待500ms，保留透明度严格检查。旧浅色普通按钮hover接近白色，远离目标231超过3LSB，仍会被此回归检出。

保留失败过程证据，不能把这些日志当通过：

- `build-first-result.json`：退出1，48.7210141秒；新增测试的NavigationState{}触发MSVC C2512（成员NavigationTween explicit构造与aggregate初始化冲突）。修为普通局部默认初始化后赋值；`build-final-result.json`退出0。
- `headless-final-result.json`：退出1，118.2099657秒，21个新增测试断言失败；原1LSB过于严格，真实渐近量化端点约2LSB。修正为3LSB且增加held/hover距离、实际alpha检查，未改vendor/生产。
- `build-verified-result.json`退出0；`headless-verified-result.json`退出1，118.940852秒，仅18个新增移出alpha检查失败：20帧333ms后实际alpha5，尚未稳定。仅将本轮移出等待延长到30帧，alpha<=3保持，最终accepted均为alpha2并完整通过。

## 验证边界

没有启动GUI、窗口或Computer Use；实际桌面鼠标观感及最终截图未验收。无窗口结果证明真实控件输入、DrawData颜色/透明度、三倍率与单次切换，不等价于真实DX11窗口截图。父任务继续进行中，本轮新改动保留未提交。
