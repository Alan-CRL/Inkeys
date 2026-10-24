# 首轮验证记录（动画修订见 research/animation-validation.md）

日期：2026-10-24。用户预先批准规划与实现，并明确排除设置窗口文字渲染。全程仅CLI和无交互窗口验证，未执行commit/push。

## 最终结果

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| 完整InkeysRepo.sln Debug\|ARM64 | exit0，20.27秒，0 errors / 3 warnings | .trellis/.runtime/ui-font-pixel-rendering/solution-build-final.log及.result.json |
| 新产物Headless --no-window | exit0，84.25秒，PASS animation correctness | .trellis/.runtime/ui-font-pixel-rendering/headless.stdout.log及.result.json |
| 新产物Inkeys --bar-eraser-offscreen-test | exit0，28.89秒，[EraserVisual] failures=0、[PageControlUI3] failures=0 | .trellis/.runtime/ui-font-pixel-rendering/font-offscreen.*与Build/eraser-b/offscreen-results.log |
| 三语sync/check | 英文/繁中各437/437，通过 | Scripts/i18n.ps1输出，生成物仅新增本次键 |
| git diff --check、编码/BOM/CRLF | 通过 | 15个tracked改动保留原工作树格式，新header UTF-8无BOM/CRLF |
| Trellis context validate、完整范围审查 | 通过 | research/check-review.md与implement/check各8条上下文 |

完整构建由vswhere动态定位ARM64原生MSBuild，使用/m:2、/nr:false、PreferredToolArchitecture=arm64，15分钟预算；同一PowerShell invocation先规范化PATH并禁用节点复用。未修改solution、工程、SDK、工具链或第三方代码。最终3条warning均来自既有hashlib++的类型转换；前一轮完整重编译的193条warning也没有落在本任务新增/修改行。

Headless产物时间为06:29:04.999 UTC，晚于两个新增Headless测试源；之后仅修复产品离屏测试访问方式，Headless源未再变，不重复无必要全量测试。产品EXE为06:31:57.829 UTC，晚于最后离屏测试修复；离屏运行使用该新产物。

## 本次覆盖

- 同源生产helper：48DIP=0.6、DPI/自由倍率/动画组合、向下截断而非round、整数、0至1px、非法输入、开启还原、不反向放大。
- 真实Config：默认/缺失/非法类型、true/false读写及新实例重读、Mini隔离、复制冻结、Reset与NoUpload。
- 生产Word：真实BGRA输出与独立推导整数物理字号的参考绘制完全相同；小数字号输出不同；切回后输出恢复。包含内容缩放、DPI、非均匀DC变换和PIXELS模式。
- 真实细刻度DWrite布局：最终字号、外层变换后的缓存键、策略切换后失效；旋转/剪切下Y轴设备长度解析。
- 生产Scene：同frameTime策略变化产生damage；隐藏时先消费SetOpacity几何damage，再独立验证新策略产生damage，避免测试被其他dirty遮蔽。
- 设置仅新增卡、115DIP父容器高度、原子值及既有轻请求/FIFO保存；字体图集/尺度/渲染方式未变。

## 编译中修复

沙箱外首轮完整编译exit1，首个有意义源码错误为新增Bar.EraserAttribute.Test.cpp直接访问protected GetThicknessFineDialLabelLayout（C2248，共4处）。已仅在测试块中派生FontTestRenderer并公开继承构造/该方法，不扩大生产API、添加friend或改变module边界。修复后完整solution复编通过。

## 本机/Codex环境与基线

- 修改前Headless --no-window基线exit0，66.66秒。
- 沙箱内首轮solution在依赖扫描后301.36秒exit1，日志0 errors / 0 warnings；15分钟监督未触发超时。只读进程核对确认构建已退出。其表现与仓库已有Codex runner故障记录一致。
- 同一脚本/配置在沙箱外即正常扫描并编译，取得上述真实测试源码首错；源修复后仍用相同配置成功。没有为环境问题改产品源码或项目配置。
- PATH规范化后vcpkg applocal未找到pwsh.exe，但其现有targets自动回退系统Windows PowerShell并成功；完整MSBuild exit0。未修改第三方脚本。
- 测试TEMP/TMP仅对本次进程指向仓库scratch，避免既有Codex临时目录MoveFileEx权限问题；未改产品配置写盘器。
- 仓库core.autocrlf=true，Git对象中的LF为正常规范化；用git cat-file --filters HEAD:path对照原工作树BOM/换行，全部保持CRLF及原BOM。

## 验证边界

设置ImGui文字、启动预览、字体与抗锯齿模式未改。整数em高度不等同逐字形像素对齐；数值/离屏通过不代表真实大屏清晰度观感已验证。按用户规则未启动GUI或Computer Use，实机观感由用户独立观察。

实现和自动验证已完成。随后用户明确授权为本次改动创建本地commit，同时保持任务进行中，不finish、不归档、不push；以上“未commit”为实现验证阶段的历史记录。
