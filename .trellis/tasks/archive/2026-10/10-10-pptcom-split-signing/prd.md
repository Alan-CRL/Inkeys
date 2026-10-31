# PptCOM 构建拆分
## 目标与验收
拥有.github/workflows/build-windows.yml及必要workflow验证/资源校验PowerShell脚本。
PptCOM Windows2022 Release/AnyCPU无submodule/vcpkg；native矩阵Windows2022只构建vcxproj且needs完整DLL准备，无.NET4/NuGet。
同次DLL与TLB独立artifact；构建、上传和下载失败必须使job失败。
按artifact ID传递，清理限定current run，重新运行避免消费旧产物。
