# 构建环境排查

2026-10-28；保持现有MSBuild/工具链、Solution和Debug|ARM64，不修改项目配置绕过环境问题。

1. 初次 `/m /nr:false` 基线构建的 `baseline-build.log` 最终记录Solution FAILED、0 warnings、0 errors、恰好5分钟；主vcxproj已链接，但其余项目没有启动记录。runner session 81889随后已不存在。该轮不能当完整构建通过，也没有可定位的源码错误，未重复保留并发构建。
2. 改用完整Solution `/m:1`、同invocation保存并规范化单一Path、hidden进程重定向和显式结果JSON。`perf-build-result.json`记录exit1；PptCOM/托管测试及Headless均完成，主项目第一有意义错误为 `C1902: Program database manager mismatch`，位于新编译测试单元的1:1。
3. 当前v143默认工具集14.44.35207，实际cl为HostArm64/arm64。仓库先前已针对同一安装完成独立全新 `/Zi /FS` 探针的沙箱内外对照，见 `archive/2026-10/10-18-pptcom-dynamic-compatibility/research/verification.md:41-44` 和 `archive/2026-10/10-11-redo-popup/research/verification-20261012.md:22-24`：版本匹配，沙箱内C1902、相同命令正常用户执行环境exit0，指向PDB服务/IPC执行边界。
4. 后续相同Solution构建使用允许本机PDB服务的执行环境，由工具自动审批；不关闭调试信息、不更换编译器、不清理用户全局PDB服务或改写Git/SSH配置。结果以实施验证报告的真实退出码为准。

vcpkg的pwsh未在PATH时曾正常回退Windows PowerShell；该条9009本身不是最终构建结论。此记录不把C1902文字单独当作版本不兼容证明。
