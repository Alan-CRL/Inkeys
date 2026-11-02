# 实施计划

## 启动门槛

- [x] 用户已明确批准本版规划，任务已激活实施。
- [x] 复核feature/i18n、相对8bf8f41d变化和用户已有改动；读PRD/design/research及对应规范，记录原文件编码/BOM/换行。
- [x] task validate通过后激活实施；python alias不可用时使用研究记录中的现有运行时，不安装依赖或修改全局配置。

## 顺序与最小文件边界

1. **身份／资源和显示排序（AC1）**：IdtI18nLocales.h追加九项，新增由同表生成的代码升序显示索引；Setting.cpp语言页双向映射显示行和保存值，pending/失败后选中项一致。旧11保存值不变；RC/resource及必要vcxproj/filter/sln文件展示项登记，不改构建配置。
2. **翻译（AC2/AC3）**：创建九个JSONC、正确Language；sync后按优先级完成七语、两地区必要差异。保留679键/顺序/token/换行，生成头只由脚本维护；固定槽先精炼新增译文，旧11语不全面重译。
3. **默认按钮（AC3）**：MessageBox.Layout.cpp补七语，地区英/西复用，保持自定义覆盖及快照；message_box_tests.cpp补四按钮预期、局部覆盖和特殊字符正文／最窄窗GDI+无窗口测量。
4. **身份/格式回归（AC1/AC3）**：IdtI18n.cpp原invalid11改20；固定预期覆盖全部旧11和新增9、有效19/非法20、唯一代码/LANGID/JSON资源、精确匹配及回退。清点活动format消费者，保留真实类型含UTF-16/浮点/字符串/IdtAtomic、swprintf边界、事务/并发/未知资源失败。
5. **排序回归（AC1/AC4/AC5）**：在既有设置测试验证20项固定预期升序、无重复遗漏、双向往返、每项实际请求身份，旧值0=en-US而显示行0=de-DE，失败恢复／重启选择不串位。页面矩阵语言下拉与当前选中值同步采用共享排序视图，打开滚到底/回顶/关闭不改变配置。
6. **字体／布局（AC4）**：setting_design_tests.cpp、setting_font_benchmark_tests.cpp补九语样本及必要断言。真实Regular/Bold最终全文、20项母语、Caption12/Control13/Body14/标题、重音／下伸部／字面与H数字基线；JP/KR主脸验证Harmony补缺，保留旧校准负对照。
7. **切换（AC5）**：复用Setting.cpp既有生产生命周期专项20语3轮和故障／重试，覆盖或补同族快路径、新语↔TC/JP/KR。保留Bar/PageControl对象／状态／真实刷新，不重建整UI。
8. **实际页面（AC4/AC6）**：全20语100/150/200%浅深、窄宽组件／长说明／DDB／主页／语言列表；显式扩展当前过滤逻辑让新九语125/187.5%实际执行，保留中英日韩400%极端回归。看PNG和字形数据，不只看无溢出。

条件文件：Setting的Controls/Design/Layout/Typography、MessageBox.Window、Bar/PageControl与其测试，仅在新译文真实诊断需要时最小改动，记录locale/key/尺寸。IdtMain/IdtConfiguration已消费Locales，预期无需修改。不存在缺口就不改FreeType/后端/字体源/线程/依赖；相关spec收尾时仅更新本次稳定合同。

## 验证命令和执行方式

实施完成后运行，不把旧二进制或本轮只读基线当作新增语言通过：

```powershell
pwsh ./Scripts/i18n.ps1 sync
pwsh ./Scripts/i18n.ps1 check
```

构建前通过vswhere动态定位当前VS，使用ARM64原生MSBuild。PATH workaround与命令放在同一PowerShell invocation，完整solution解析依赖，不单独构建Inkeys项目：

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
$i18nVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$i18nVsRoot = & $i18nVswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
$i18nBuildTool = Join-Path $i18nVsRoot 'MSBuild/Current/Bin/arm64/MSBuild.exe'
if (-not (Test-Path -LiteralPath $i18nBuildTool)) { throw 'ARM64 MSBuild unavailable' }
& $i18nBuildTool InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Solution build failed: $LASTEXITCODE" }
```

总超时至少5分钟，建议10分钟；用runner session/poll或隐藏Start-Process及日志执行，不把短yield当进程超时，不开交互窗口。读取退出码和第一个有意义错误，区分源码/链接/依赖/本机问题，不改配置或升级依赖绕过环境。

构建通过后用本次二进制串行执行：

```powershell
./Build/ARM64/Debug/Inkeys.exe --i18n-locales-test
./Build/ARM64/Debug/Inkeys.exe --setting-font-lifecycle-test
./Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test
./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window --setting-font-pages-only
./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window --setting-font-benchmark-only
git diff --check --ignore-submodules
git diff --stat --ignore-submodules
```

前三入口已确认GUI初始化前返回，实施前复核；禁止无参数启动产品。Headless预留数分钟、矩阵更长，每60秒内更新有意义进展；通过项目不无条件重复全跑。独立benchmark用已有locale/scale过滤聚焦新样本，不重做旧字体性能研究。

必要聚焦入口：`--no-window --setting-general-only` / `--setting-copy-only` / `--setting-attention-only`；页面支持 `INKEYS_FONT_PAGE_LOCALE` / `INKEYS_FONT_PAGE_SCALE`。仅失败或新变化需要时复跑。`--ui3-multitarget-benchmark`有旧任务culling失败，仅本次修改Bar刷新或证据不足时复核，不放宽阈值或将历史失败冒作当前HEAD基线。

## 交付和收尾

- [x] 运行trellis-check审查全部本次范围；核对原始检出编码/换行和diff，无大面积无关修改。
- [x] 用trellis-update-spec判断并更新已验证的新数量/索引/显示排序合同，不改无关规范。
- [x] `validation.md`记录新二进制基线、命令/退出码/关键日志、20语覆盖与离屏产物；字体预检查不可代替实际渲染结果。
- [x] `translation-review.md`记录逐语完成率（预计九语各679）、es-MX/en-GB逐key差异和理由、短译取舍、需要母语者审校的具体术语与长句。
- [x] 汇报新增九语、排序和索引兼容、完成度、字体/布局/切换/构建结果，以及未做GUI实机和母语审校的边界。不自动commit/push或归档尚未完成任务。
