# 验证记录

## 已执行（贴底复核修正前的第一轮）

- 原生ARM64 MSBuild完整 `InkeysRepo.sln Debug|ARM64`：exit0，45.464s，日志 `Build/settingui-style-motion/build.log`。现有hashlib++/HiMsg等warning保留，没有为清零warning修改无关代码。
- 新增定向motion过滤后的增量完整solution：exit0。真实页面/行/条件块回放：100%和187.5%各241帧，动作各1次，exit0；日志 `motion.log`。
- 完整Headless `--no-window`：exit0，249.215s，stderr为空，末尾PASS；测试子进程TEMP/TMP位于仓库内Build scratch，不修改产品写盘路径。日志 `headless.log` / `headless.stderr.log`。
- 主程序既有非交互 `--i18n-locales-test` 与 `--setting-font-lifecycle-test`：均使用Hidden+Wait获取真实ExitCode=0，20语资源/格式化与字体生命周期failures=0。第一次直接调用GUI-subsystem exe没有可靠等待/ExitCode，未算作验证，已用上述等待方式重跑。
- `pwsh ./Scripts/i18n.ps1 check`：exit0，19份非基准语言均679/679。
- 原生/颜色工作者的独立生产边界探针见native-implementation/color-implementation：颜色修前失败、修后32场景17664帧通过；resize计数减少，但不能声称真实Win7/DXGI性能改善。
- `git diff --check`通过。16份已跟踪变更文件对照经Git filters后的HEAD工作树版本，BOM/EOL保持；不能直接将Git blob LF误判为原工作树LF（仓库core.autocrlf使工作树原有CRLF）。

## 全范围复核发现并要求补验

第一轮完整测试未覆盖真实贴底插入。独立checker编译实际Controls/ImGui/ImFluent无HWND探针，原代码15个过渡帧全部贴底偏离，最大124px且最终失去贴底。当前按真实条件高度预推进和原生Begin前同步内容/scroll修正；需要补回归后再次构建/定向/完整验证。

同时核对隐藏thumb的实际点击：取消区域必须含完整正文轨道，不能只含content ClipRect。最终结论以后续记录为准，不能因第一轮全绿而跳过新发现。

## 尚未执行的实机项目

没有GUI/Computer Use授权，未打开设置窗口，未验证真Win7连续resize及Win10/11动态桌面上maximize/restore观感。无窗口回归与资源回调计数不能替代这些证据。未commit或push。

## 复核修正进展

贴底插入/移除的生产探针已从15个失败过渡帧修正为0，footer各帧保持272px；最终仍须等待集成构建回归。源状态显式传入正文Begin，避免开屏复用帧没有Begin时留下全局prepared裸指针。产品ConfigurePageMotion传入真实I18n::revision，并按字体/倍率/尺寸变化失效预测高度，不重播导航。

## 扩展离屏与分数倍率复核

新增真实WARP样张函数第一次构建因FreeTypeTextFontConfig漏传第三个参数而C2660，已在测试源修正；随后完整solution exit0。带motion snapshots的定向回放生成20份入场/插入/退出/普通首页样张，渲染均0失败；但集成CPU夹具仍有25个贴底断言失败，不能算整套通过。

定位：187.5%请求1312.5px被ImGui截为1312，原缓存比较原始请求会每帧误判resize；另有EndGroup把完整LastItemRect和额外spacing计入父条件高度，嵌套父级测量不随子块收缩。另经实际水平滚轮证实正文ScrollMax.x=8，现统一InnerRect及scroll预算。上述修正等待最终真实集成回归，未降低原断言容差。

已人工查看离屏appearance-light-100-0.png和home-dark-187-4.png：文字/卡片大小、对齐和clip正常；这是无HWND离屏样张，不是系统桌面模糊或Win7观感验证。

## 最终通过结果（所有产品修正后）

- 完整原生ARM64 `InkeysRepo.sln Debug|ARM64`：exit0，16.108s，`build-verified.log` / `build-verified-console.log`。没有改变SDK、依赖、设备/交换链模式或字体文件。
- 定向 `INKEYS_SETTING_MOTION_ONLY=1` + `INKEYS_SETTING_MOTION_SNAPSHOTS=1` + Headless `--no-window --setting-home-only`：exit0，18.403s，stderr为空；100%/120%/187.5%各405帧，共1215帧，真实动作各1次。贴底插入/移除、嵌套显隐、反向、稳定宽度、首帧与水平滚轮、隐藏轨道点击、异步插入单调、字体/语言/几何失效均通过。日志 `motion-verified.log`。
- 20份真实DX11/WARP离屏场景全部failures=0：2倍率×2主题×5阶段/页面；是20个场景而非20语图像。最终样张在`Build/settingui-style-motion/visual/`，另查看appearance-dark-187-2.png确认动态行及后项布局/字体/轨道正常。
- 同一最终二进制完整Headless `--no-window`：exit0，252.537s，stderr为空，末尾PASS。日志 `headless-verified.log` / `headless-verified.stderr.log`。
- 最终主程序非交互资源/格式化及字体生命周期两入口：Hidden+Wait，exit0；分别`i18n-verified.log`的failures=0与`fonts-verified.log`的failures=0。
- 额外120%发现的padding边界已纳入：保留浮点padding，由SetNextWindowContentSize统一取整；不能先把两侧padding各自截断再相减，避免制造不足2px的虚假X范围。只读checker按实际vendor公式复核通过，实际1215帧验证通过。
- 既有橡皮演示回归在旧任务路径生成10张未跟踪PNG，已验证绝对路径位于工作区、名字白名单且无跟踪文件后逐个移动至ignored `Build/settingui-style-motion/eraser-demo-visuals/`。未递归删除/移动旧任务或用户文件。

自动验证已全部通过；实机Win7 resize与Win10/11动态桌面最大化/还原仍未验证。没有任何commit或push，任务保留等待用户审阅及人工验收。

## 阶段性提交授权（2026-11-03）

用户要求先提交改动但不结束任务。本次代码、测试、规范及任务记录合并为一笔commit；任务保持in_progress，不执行finish/archive，不push，人工验收项目继续保留。
