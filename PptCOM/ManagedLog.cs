using System;
using System.IO;
using System.Runtime.InteropServices;

namespace PptCOM
{
    // C# 侧日志不再写 Console：经 IPptCOMLogging 注册的原生函数指针进入 Inkeys 统一日志（C++ 唯一写文件线程）。
    // 与 Inkeys/IdtPlug-in.cpp::PptComLogCallback 共享的常量（两侧必须保持一致）：
    //   level ：0=Debug 1=Info 2=Warn 3=Error 4=Critical
    //   module：低 8 位为托管模块号（0=PptCOM）；0x100 位表示 PptCOM 调试通道记录（不受日志等级过滤）
    internal static class ManagedLog
    {
        internal const int LevelDebug = 0, LevelInfo = 1, LevelWarn = 2, LevelError = 3, LevelCritical = 4;
        internal const int ModulePptCom = 0;
        internal const int ChannelFlag = 0x100;

        [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        internal delegate void NativeLogCallback(int level, int module, string text, int length);

        // 委托只包装进程寿命的原生静态函数，不需要防止托管回调被回收；未注册时直接丢弃。
        private static volatile NativeLogCallback callback;
        // 仅测试替换输出出口，不替换门控或业务路径。
        private static volatile TextWriter testOutput;

        internal static TextWriter TestOutput
        {
            get { return testOutput; }
            set { testOutput = value; }
        }

        internal static void SetCallback(long pointer)
        {
            try
            {
                // .NET 4.0 只有非泛型重载；0 表示撤销。
                // 32 位进程（LargeAddressAware）的高地址会被原生侧零扩展成正 Int64，new IntPtr(long) 会溢出，
                // 因此按指针宽度截回 32 位再构造。
                IntPtr address = IntPtr.Size == 4 ? new IntPtr(unchecked((int)pointer)) : new IntPtr(pointer);
                callback = pointer == 0 ? null : (NativeLogCallback)Marshal.GetDelegateForFunctionPointer(
                    address, typeof(NativeLogCallback));
            }
            catch { callback = null; }
        }

        // PptCOM 调试开关（SetConsoleOutputEnabled）控制的诊断；返回 false 仅表示输出出口抛出异常。
        internal static bool Channel(int level, string text)
        {
            if (!PptComDiagnostics.Enabled) return true;
            return Write(level, ModulePptCom | ChannelFlag, text);
        }

        // 不受调试开关控制的普通分级日志（如 INKEYS_PPT_PERF），由原生侧按日志等级过滤。
        internal static bool Write(int level, string text)
        {
            return Write(level, ModulePptCom, text);
        }

        private static bool Write(int level, int module, string text)
        {
            if (text == null) return true;
            try
            {
                TextWriter test = testOutput;
                if (test != null) { test.WriteLine(text); return true; }
                NativeLogCallback target = callback;
                if (target != null) target(level, module, text, text.Length);
                return true;
            }
            catch { return false; } // 日志失败绝不改变 COM 业务返回值或 HRESULT。
        }
    }
}
