# Touch注册诊断版：下一轮设备测试

本版补充注册与Pointer来源证据，保留此前五窗Touch修复。当前只产生Pointer/兼容Mouse且无可用Touch时的按钮业务缺口尚未修复，仍可能点不动；这轮先定位注册/输入协议原因。

## 1. 开启输出，保留现有驱动设置

使用Build/x64/Release/Inkeys.exe。在设置 → 实验选项 → 控制台输出勾选“测试触控反馈”，然后完整退出软件；此前已开则保持并退出旧进程。不要只关闭设置窗口。先保持当前数位板设置，能查看时记下该设备针对Inkeys的Windows Ink状态（开/关/不知道），不要只看全局设置而忽略程序配置。

## 2. 在CMD启动采集

```cmd
cd /d "D:\Project\Inkeys\Repo\Inkeys\Build\x64\Release"
start "" /b /wait ".\Inkeys.exe" > ".\M6-touch-registration.txt" 2>&1
```

在CMD运行；若测试包放在其他目录，先切到实际exe文件夹。stdout/stderr均保存到该文件。重复本轮前保留已有文件，不能覆盖唯一失败日志。

## 3. 一轮最小操作

- 用同一块曾复现问题的数位板，先点主栏，再点左右白板分页；随后切PPT并覆盖当前可见的另外分页位置。场景切换若笔无法操作，可以用鼠标辅助；说明哪些操作用了鼠标即可。尽量覆盖四个分页窗口，设备/布局没有显示某窗时不强求。
- 笔保持在感应范围内往返点击几轮；失效时同一目标再点2次，随后远离数位板并返回，再点一次。无响应也继续采集，Pointer Down本身就是本轮关键证据。
- 若电脑有手指触屏，可额外用手指点主栏一次，作为Touch对照；没有就跳过。
- 完整退出软件，等待CMD提示符返回。

## 4. Windows Ink对照（方便时做，不要求现在搭环境）

若能确认当前该程序Windows Ink为关闭状态，先保留上面原状态日志；然后开启该程序的Windows Ink，按驱动要求重启驱动及软件，用同样命令把输出文件名改为M6-touch-registration-ink-on.txt，再重复上面点击。若当前已开启或无法确认，先交回原状态日志，不必为了测试盲目切换/重装驱动。

## 5. 交回内容

发送完整M6-touch-registration.txt（有对照则同时发送ink-on文件），并说明数位板品牌型号、Windows Ink原状态/是否改动、哪些目标无响应及远离后是否恢复。不要只复制最后几行；注册记录在软件启动时输出。

新增记录将包含注册调用返回值/失败错误、IsTouchWindow状态、窗口owner线程，以及Pointer Down时的注册复查/Pointer类型/源设备/目标窗口。新增stage为touch_registration、touch_window_snapshot（after_registration/pointer_down）、pointer_info（queried/query_failed/api_unavailable），构建指纹见本轮验证记录；已有ui_schema/ui_input v1、掉样统计仍保留。日志不直接读取厂商Windows Ink复选框，状态需按实际驱动配置描述。
