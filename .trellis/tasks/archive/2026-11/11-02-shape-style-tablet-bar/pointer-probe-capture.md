# 阶段1 Pointer 探针：一次实机采集

本版是**探针**，不是修复版。启用后，五个UI窗对“接触中”的手指/笔 Pointer 消息（按下、接触移动、抬起）处理后直接返回，不交系统默认处理，用来验证系统是否因此不再派生 WM_TOUCH 和带签名的兼容鼠标。**探针启用期间，手指和笔点不动主栏/分页是预期现象**（业务暂未接入 Pointer），请用真实鼠标操作和退出。悬停消息不受影响。默认不启用，不设环境变量时行为与上一版完全一致。

## 1. 开启输出

使用 Build/x64/Release/Inkeys.exe。设置 → 实验选项 → 控制台输出勾选“测试触控反馈”，完整退出软件（只关设置窗口不够）。探针需要这个输出开关和下面的环境变量同时存在。

## 2. 在 CMD 启动采集

```cmd
cd /d "D:\Project\Inkeys\Repo\Inkeys-draw\Build\x64\Release"
set INKEYS_POINTER_PROBE=1
start "" /b /wait ".\Inkeys.exe" > ".\M6-pointer-probe.txt" 2>&1
```

文件名按设备区分（例如 M6-pointer-probe.txt、old-tablet-pointer-probe.txt、finger-pointer-probe.txt），不要覆盖已有日志。日志开头应出现 5 条 `stage=probe_mode reason=enabled`；没有说明环境变量或输出开关没生效。

## 3. 一轮最小操作（每块设备各做一次）

用笔（或手指）完成以下动作，停顿之间留 1–2 秒：

1. 点主栏几个按钮、点左右分页各几次（预期没有响应，这是探针预期）。
2. **按住后拖出窗口**：在主栏按钮上按下，保持按住拖到主栏之外（拖到画布空白处或另一个分页上方），再抬起。主栏和分页各做 2 次。
3. **长按不动**：在主栏按钮上按住不动约 2 秒再抬起，观察屏幕是否出现“长按圆圈”或右键菜单，并写下现象。
4. 笔保持悬停，在主栏、分页和画布之间来回移动几次（不接触）。
5. 用真实鼠标点主栏几次，确认鼠标仍能正常点击（鼠标不受探针影响）。
6. 用鼠标完整退出软件，等待 CMD 提示符返回。

没有手指触屏、没有旧板的，跳过对应设备即可。

## 4. 交回内容

- 完整日志文件，说明设备（品牌型号、笔还是手指）、Windows Ink 原状态（不知道就写不知道）。
- 第 3 步是否出现长按圆圈/右键菜单，以及第 2 步拖出窗口时有没有看到异常。

## 5. 判定指引（供分析）

日志新增 stage：
- `probe_mode`：启用时每个窗口启动输出一次。
- `probe_contact reason=swallow_down/swallow_update/swallow_up`：被探针吞掉的接触阶段 Pointer。字段 `known_down`（该接触的按下是否被追踪）、`same_window`（本次是否仍在按下的窗口）、`down_window`、`in_receiving_rect`/`in_down_rect`（此刻屏幕点是否仍在接收窗/按下窗矩形内）、`swallowed` 累计、`legacy_touch_seen`、`legacy_signed_mouse_seen`（探针启用后看到的 WM_TOUCH 与带 Pointer 签名的左键兼容消息累计，仅统计窗口线程入口）。

| 观察 | 结论 |
| --- | --- |
| `legacy_touch_seen` 与 `legacy_signed_mouse_seen` 对接触全程为 0 | “接触阶段不转交 DefWindowProc”成立，采用主方案 |
| 仍有增长 | 系统在不转交时仍派生副本；采用设计的类型排除兜底，长按右键合成是否仍存在看第 3 步现象 |
| `swallow_up` 出现 `same_window=0`，或 `known_down=0` | 抬起没有回到按下窗口，先补 Pointer 捕获设计再接业务 |
| `same_window=1` 且 `in_down_rect=0` | 拖出窗口后抬起仍回到按下窗口（隐式捕获成立） |
