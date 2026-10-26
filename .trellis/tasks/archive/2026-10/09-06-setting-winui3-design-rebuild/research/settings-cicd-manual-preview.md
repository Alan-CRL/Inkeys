# 置顶间隔秒表与 CI/CD 人工预览（2026-10-24）

当前用户明确要求可在实际版本页进入 CI/CD 页面查看。此授权更新上一轮“测试夹具不得进入生产元数据”的临时执行边界：这次直接在现有 signal1 元数据定义内填写有标记的本地预览数据，保留到用户确认测试结束，不提前清除；正式 CI 注入协议和渲染切换不变。

范围仅为 Setting.Pages.cpp 的 top-interval 图标从 U+E922 改为与 DDB 间隔相同的 U+E916，以及 Setting.cpp 的现有 settingCICD 字段初始化。url 非空即由既有 Version.Info.AutoBuild / CICDInfo 分支提供真实导航。测试链接使用本仓库 actions / repository，不假冒具体实际运行；提交者注明本地预览。用户称头文件，但当前真实定义已经在 Setting.cpp，不新增/搬移结构。

实现与静态审查已完成。完整现有 `InkeysRepo.sln Debug|ARM64` 使用动态定位的 ARM64 原生 MSBuild、同 invocation PATH workaround，在正常主机权限下构建，实际退出 0。日志：`settings-cicd-preview-build.log` / `settings-cicd-preview-build-console.log`。既有 vcpkg app-local 先尝试 pwsh.exe 返回 9009 后，自动使用 Windows PowerShell 成功，属于本机 PATH 提示；未改源码/工程绕过。现有转换 warning 未顺手清理。

最终程序 `Build/ARM64/Debug/Inkeys.exe` 已包含本地预览提交者、actions URL 与多行工具链说明（纯二进制 UTF-16 检查，没有执行程序），SHA-256 `a0eca204cd3aefda62e348ef615a5cf1e7b5bdee0b5c922b52b84720930d18b9`。

两个源文件继续为 UTF-8 无 BOM / CRLF；去掉预览元数据并按工作区换行比较后，Setting.cpp 与上一轮源码 SHA-256 完全一致，Setting.Pages.cpp 恢复该单一图标也与上一轮一致。`git diff --check` 通过，两张既有 CSO 字节保持。仅改元数据初始化与已存在字形，不新增或重跑镜像常量的测试，也未重跑上一轮已通过的完整无窗口套件；此增量没有行为逻辑变化。永久 spec 无新增合同，临时预览不应记作正式 CI 默认值。

**待人工查看/清理**：本轮保留字段和新编译程序，等用户实际查看完成后仅移除 signal1 struct 的十个初始化值及临时注释，再编译恢复手动构建默认分支。用户实际查看与之后清除数据属于待办；本轮不启动 GUI、不 commit/push，原有未提交变更及两张 CSO 保持。

## 独立只读审查

本轮仅核对 signal1 元数据预览初始化和 top-interval 图标追加；未修改代码、构建、GUI、运行测试或 commit/push。未发现阻断问题。

- U+E916 与 DDB 拦截间隔一致；只改图标，不改选项、事件、字段或保存逻辑。
- settingCICD.url 非空会沿用现有 AutoBuild/CICDInfo 分支，原版本页可导航到真实 CI 详情页；Setting.cpp 的动作路由和 RenderCicdDetails 字段绑定未变。
- 链接为真实仓库的 actions 总页及仓库首页，没有捏造特定 run ID；提交者、日期、runner 版本和工具链文字明确标为本地预览/测试数据。
- 两处 // signal1 和原结构/字段/对象名称保持；现有 build-windows.yml 按这对标记替换整个元数据块的协议继续适用。
- 源码采用原有宽字符串初始化，换行工具链内容仍为合法的 \n 转义，未新增全局开关、包装字段或页面逻辑；原有未提交实现保持。
- 用户已明确授权将预览值留在实际页面供人工查看，故本次临时数据不按上一轮“测试夹具仅在测试中”要求判为问题；待用户确认查看结束后再清除，本代理未提前清理。

以上仅是静态审查结论；构建由主会话执行，实际页面外观和后续清除数据仍待用户查看确认。

## 用户查看后要求清理（2026-10-24）

最新用户附图确认已实际查看页面，现要求去除返回箭头旁的“软件版本”文字链接，并清除全部 CI/CD 人工预览值/注释。此请求结束上文的保留阶段，不删除正式 CI 页面/链接逻辑；仅将两处 signal1 之间结构恢复 HEAD 的空字段原状。按 build-windows.yml 的真实替换正则和结构模板做内存模拟，原样保留 workflow；不为测试写回虚假生产数据。

本次实现和独立审查已完成：仅 Setting.cpp 删除三个文字链接布局/动作行、恢复空元数据；IconButton 返回逻辑及所有实际 CI 页面/构建链接分支保留。两处 signal1 之间的结构逐字节等于 HEAD 块（按原工作区 CRLF 比较），十个 wstring 均未初始化，所有预览值和临时注释已去除。

使用 PowerShell 从当前 build-windows.yml 实际“填充 CI/CD 变量”步骤提取正则和 STRUCT_BLOCK，在内存中按相同 -join CRLF / -replace 执行：原空块只匹配一次，十字段及 run URL 完整注入，模拟前后磁盘源码 SHA-256 不变。workflow 未修改，也未将模拟值写回代码。

完整 InkeysRepo.sln Debug|ARM64 在 ARM64 原生 MSBuild 下编译成功（exit 0），同 invocation 使用 PATH workaround。日志 settings-cicd-cleanup-build.log / settings-cicd-cleanup-build-console.log；既有数值转换 warning、vcpkg 首选 pwsh 不在 PATH 后的 Windows PowerShell 回退提示没有为此改工程或依赖。最终 Inkeys.exe 纯字节检查不含四组人工预览文本，证据为 settings-cicd-cleanup-verification.json。

UTF-8/BOM/CRLF、最小差异与 git diff --check 通过；恢复已删三行后源码 SHA-256 与前轮基线一致，两张既有 CSO 字节保持。此为删除重复导航与临时初始化的低影响改动，未新写镜像测试，也未重复运行上一轮通过的全套无窗口回归；未启动 GUI/Office/Computer Use，未 commit/push。原有 CI 元数据规范继续适用，无新增生产合同。上文保留预览阶段已结束，清理待办完成；父 Trellis 任务与历史剩余实机验收保持不变。

## 清理增量独立审查

已依据最新 PRD、实施计划和用户查看后清理要求，只读复核本次清理，无发现。未构建、运行程序/GUI、修改代码、commit/push。

- CI/CD 顶部已去掉“软件版本”HyperlinkButton 和对应 SameLine，仅保留原 ##cicd-back 箭头及回到 tab6 的路由；详情字段绑定和真实外链动作保持。
- signal1 标记恰好两处，十个 wstring 字段均为空定义；按工作区换行归一后整个标记块与 HEAD 完全一致。临时预览注释、提交者、runner 版本和工具链测试说明均已从生产源码清除。
- build-windows.yml 没有差异；真实匹配表达式仍以两处 signal1 定位整个结构，STRUCT_BLOCK 的十字段/对象名及后续替换步骤与清理后的结构兼容。主会话继续执行真实 PowerShell 表达式的内存验证，本审查未改 workflow 或注入任何测试源数据。
- Setting.cpp 保持 UTF-8 无 BOM、全部 CRLF；旧未提交功能和永久秒表图标不在本次清理中撤销。
- 本文前面的“保留预览”是已结束的历史阶段；当前结果遵循用户最新删除要求，不再保留测试初始化。完整编译和产物不存在预览值的检查由主会话记录。
