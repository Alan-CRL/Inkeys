# 集成与发布检查设计

- 以现有 InkeysRepo.sln 为唯一主程序构建入口，按 PptCOM→Inkeys 项目依赖；PowerShell 同调用中规范 PATH，定位 VS ARM64 原生 MSBuild。构建与测试串行占用输出目录。
- Debug|ARM64 是基本门禁；Release 与 Win32/x64/ARM64 按实际项目矩阵扩大。项目/ABI/HLSL/资源变动使用合适 clean Rebuild，检查 DLL/TLB、shader、资源和实际产物。
- headless --no-window、Draw3、PptCOM 测试先查存在性/参数/真实覆盖；GUI 隐藏不等于 no-window，运行结论与编译结论分开。
- 功能矩阵覆盖 UI/输入/PPT/保存/重启/显示/fallback/冻结/设置 owner/关闭 gate，Win7 和 Office/GPU 组合按实际证据标记。
- Win7 SP1 仅 KB2670838 单列 FL11.0 硬件有/无两类设备；每类记录实际 HARDWARE/WARP、feature level、DComp 不可用后 ULW、透明/resize/device-lost/输入。微软 Platform Update 仅支持 D3D11.0 feature level，DComp 不可用；官方 API 与编译只证明静态可达性，不能代替目标系统运行。
- HF 指纹包含 HEAD 加改动/新文件内容；最终 reviewer 看所有 diff 和审计覆盖，后续修补导致旧测试失效时重跑。
