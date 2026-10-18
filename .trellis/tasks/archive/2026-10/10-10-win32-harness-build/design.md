# 设计
新增EnableUi3PresentationHarness：Debug默认true、Release默认false，显式命令行优先；定义统一宏。
剔除runner和固定测试轨迹的真实实现；必要产品连接函数禁用时等效无fixture（许可门true、安装门false、观察无副作用），不得用成功stub冒充测试。
授权入口禁用时保留识别并拒绝内部参数的最小逻辑，产品启动不变。
native工程补SolutionDir默认根目录并移除两处ProjectReference；PreferredToolArchitecture未设置时64位宿主用x64/arm64相应工具，不改变目标Platform。

实施阶段用户修正：Inkeys 下 DLL/TLB 继续 Git 跟踪且不忽略，由维护者在 PptCOM 源码变化后手动同步；两个工程无自动复制或依赖。中英文编译说明同步独立构建、Harness 开关和 CI artifact 交接。
