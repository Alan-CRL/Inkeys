# Win32 Harness 修复
## 目标与验收
Release默认剔除嵌入Inkeys的UI3测试Harness，Debug及EnableUi3PresentationHarness=true保留；Win32构建使用64位宿主修复日志中C1002。
关闭Harness仍可正常链接，普通产品输入/渲染保留；内部fixture/benchmark/auth参数以非零码在窗口初始化前拒绝。
解除native工程两处PptCOM引用；独立构建目录仍为根Build。只负责Inkeys/Inkeys.vcxproj及Harness相关源/头和必要回归测试；其他sln、PptCOM.csproj、workflow由其他成员负责。
