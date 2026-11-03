# 版本基线核对

- 调查日期：2026-11-02；工作分支 `bugfix/color`，HEAD `6df7f43f`，开始时工作区干净。
- 用户测试版本：`20261028a`。本地提交 `dc8d0a13c99dcb8de8273f28d7029525cf7c7ab9`（2026-10-28 11:11:41 +0800，`Prepare to publish 20261028a`）将 `Inkeys/IdtMain.cpp:98-99` 设置为 `3.0.0-dev.12111` / `3.0.0-20261028a`，并开启 `IDT_RELEASE`。后续 `718b0ebd` 为 `Restore publish 20261028a`。
- 当前 `IdtMain.cpp:98-99` 为 `3.0.0-dev.12112` / `3.0.0-20261028a`；只凭 editionDate 无法证明用户二进制精确对应哪个提交。
- `git diff dc8d0a13 HEAD -- Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp Inkeys/Inkeys/UI/PageControl/PageControl.cpp Inkeys/Inkeys/Message/Message.cpp Inkeys/Inkeys/Business/LegacyDrawState.cpp` 无差异。
- `IdtState.cpp/.h` 自发布准备提交以来只有 49 行新增：掌擦偏好读取/保存/Host同步接口与 include；没有改动形状颜色宽度槽、getter/setter和切换逻辑。
- 因此当前调查可用于解释发布候选源码中的这两条核心路径；尚未校验用户安装包的 CI 元数据或二进制 hash，不能宣称完成该包的真机复现。

## 验证边界

仅本地历史与源码静态核对。没有访问远程、构建、运行应用或启动 GUI；没有 commit/push。任务保持 planning。
