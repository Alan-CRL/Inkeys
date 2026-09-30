# 执行清单

前置：其余六子任务交付代码/验证/未验证项；最后一次修改后不得沿用旧 PASS。出口：完整构建/测试、HF 指纹、独立总 review 与发布/人工门禁报告，不提交/归档。

- [ ] 确认主 Solution、项目依赖、MSBuild ARM64 host、配置矩阵和真实测试目标。
- [ ] 串行执行 Debug|ARM64、Release 及可得架构构建，保留命令、退出码、首因与产物清单。
- [ ] 静态核 Win7 SP1+KB2670838 的 11.1→11.0 与 Hardware→WARP、ULW 保持 FLIP_SEQUENTIAL、两种 DWM 禁用、x86/x64 导入；真机两类 GPU 缺席则留下准确人工门禁。
- [ ] 运行 no-window、Draw3、PptCOM 与跨模块测试；按能力执行静态/脚本、发布资源和 Win7 import 检查。
- [ ] 组织人工矩阵，未授权 GUI 或不可得系统/GPU/Office 组合保持需要人工。
- [ ] 清除临时产物，独立审全部 H0→HF diff/coverage；修补后复测，计算 HF 与发布门禁，交未提交报告。
