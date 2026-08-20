# OpenCV vcpkg 接入实施清单

1. 修改 `vcpkg.json`，添加四 feature 的 `opencv4` 依赖和 `4.10.0#3` override，保持 baseline 及其他依赖不变。
2. 在中英文 README、双语 TOS 和 NOTICE 的现有第三方列表中加入 OpenCV 许可与版权行。
3. 解析 manifest，检查差异、CRLF 和未预期文件；确认三套 triplet 无变更。
4. 使用 ARM64 主机版 MSBuild 执行：
   `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`
5. 检查 `VcpkgInstalled/Arm64` 中的 OpenCV 版本/features、静态库与 DLL 缺席，并检查最终 Build 输出没有 OpenCV/FFmpeg 动态文件。
6. 运行 `git diff --check` 并将构建/静态检查结果回写到本文档。

## Validation Boundary

- 构建超时不少于 30 分钟，首次 vcpkg 下载/编译时保留完整错误上下文。
- 本期不启动可见窗口；Windows 7、MSMF 和摄像头运行时验证留给后续子任务。
- 不创建 commit。

## Validation Results

- 2026-08-20：根 manifest JSON 解析通过；baseline 保持 `99a97de2cb371449d4fb9dc970f2ac562d689ec2`。
- `VcpkgInstalled/Arm64/vcpkg/status` 确认 `opencv4 4.10.0#3`，已安装 feature 仅为 `dshow`、`intrinsics`、`msmf`、`thread`。
- ARM64 静态库包含 `opencv_core4`、`opencv_imgproc4`、`opencv_imgcodecs4`、`opencv_videoio4` 等官方 port 模块，没有 `opencv_world` 静态库。
- 使用 ARM64 host MSBuild 构建 `InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`：成功，耗时 `00:16:04.55`，`0` errors，`299` warnings。
- 安装目录和 `Build/ARM64/Debug` 没有 OpenCV/FFmpeg DLL；`dumpbin /DEPENDENTS` 确认 `Inkeys.exe` 导入表无 OpenCV/FFmpeg 动态依赖。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：`PASS animation correctness`。
- 未启动主程序、可见窗口或摄像头；未执行 x86/x64 实际构建。
