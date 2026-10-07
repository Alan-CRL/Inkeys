# 完整发布 ZIP 许可文件独立复审（2026-09-29）

## 审查边界与结论

只读审 `.github/workflows/build-windows.yml` 当前 diff、上下游 Package 步骤、`.trellis/spec/native-desktop/build-and-compatibility.md` 合同，以及 `TestResults/release-hardening/license-package-check/` 的三架构隔离 dry-run ZIP。没有改 workflow、没有运行真实 Actions 或发布。对本次新增的许可复制步骤，未发现必须修补的顺序、路径或 ZIP entry 缺陷；**本地语义已验证，真实 CI 与正式 ZIP 仍未验证**。

## 逐项证据

1. 唯一 workflow diff 在第 651–659 行新增显式 `shell: pwsh` 的 `Prepare license notices for release packages`。三个目标是此前 `Prepare update folder` 创建并放入各自 `Inkeys.exe` 的 `signedUpload\\Inkeys`、`Inkeys64`、`InkeysArm64`。随后 `Prepare Tips` 将 `Tips.txt` 放入相同三个目录。新步骤依次使用 `Copy-Item -LiteralPath ... -ErrorAction Stop` 复制仓库根 `LICENSE`、`NOTICE` 和 `ThirdpartyLicenses`。失败会中止该 PowerShell 步骤。
2. 三个 `Compressed update package` 步骤在第 510–518 行，只以每个 `signedUpload/.../Inkeys.exe` 为 `Compress-Archive -Path` 输入，发生在许可步骤之前；新增目录文件不会进入更新 ZIP。三个完整发布 ZIP 的 `Compress-Archive` 在第 661–669 行，发生在许可复制之后，输入整个架构目录。压缩输出按原有通道重命名，不改变更新包命名或包内 EXE 路径。
3. 隔离 dry-run 的 `Inkeys-fixture.zip`、`Inkeys64-fixture.zip`、`InkeysArm64-fixture.zip` 各 8 个 entry：架构顶层目录下 `Inkeys.exe`、`LICENSE`、`NOTICE` 和 `ThirdpartyLicenses/` 中 5 个文件。逐项 SHA-256 与当前仓库源文件一致（例如 LICENSE 前 12 位 `230184f60bae`、NOTICE `1574fb94e1ae`、Apache License `e8de1a739345`），三个 ZIP 的目录前缀分别正确。fixture 用 26 字节占位 EXE 且不含 Tips；它只证明复制/压缩相对路径语义，不能证明正式 EXE、Tips、DLL/TLB、shader 或三架构依赖完整性。
4. `git diff --check -- .github/workflows/build-windows.yml` 退出 0；YAML 文件仍是无 BOM UTF-8、全 CRLF（孤立 LF 0）。新 step 缩进和相邻 Package step 一致，显式 `shell: pwsh`，没有依赖 Windows runner 默认 shell。当前本机无 PyYAML/actionlint，未进行独立机器 YAML parser 检查；真实 GitHub Actions 工作流及正式 artifact 尚未运行。

## 有界遗留风险

- `Upload Inkeys`/`Inkeys64`/`InkeysArm64` 早于许可复制，仅上传 EXE/PDB；这三个独立构建 artifact 不含许可，不能作为“完整发布 ZIP”对外宣称。用户本次验收针对完整 ZIP；若未来把独立 artifact 作为分发物，需要单独定随包许可合同。
- 本轮没有逐项审计 `NOTICE` 是否覆盖所有静态/动态依赖，也没有执行真正签名、上传或下载后的 ZIP 内容检查。应在真实 CI 产物上枚举三架构完整 ZIP 的 EXE、Tips、LICENSE、NOTICE、第三方许可 entry，再升级发布结论。
