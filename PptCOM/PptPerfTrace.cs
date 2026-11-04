using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace PptCOM
{
    // 只持有纯值与标准错误流；诊断不得触碰 Office，也不能依赖 ConsoleOutput.PptCOM 开关。
    internal static class PptPerfTrace
    {
        private static readonly bool configured = ReadEnabled();
        private static readonly object outputGate = new object();
        private static readonly uint processId = GetCurrentProcessId();
        private static TextWriter output;
        private static bool outputOpened;
        private static long sequence;
        private static int testEnabled = -1;
        private static TextWriter testOutput;
        [ThreadStatic] private static Context current;

        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
        [DllImport("kernel32.dll")] private static extern uint GetCurrentProcessId();

        internal struct Context
        {
            internal long Span, Request, Session, Binding;
            internal string Operation;
        }

        internal static bool Enabled
        {
            get { int test = Thread.VolatileRead(ref testEnabled); return test < 0 ? configured : test != 0; }
        }

        private static bool ReadEnabled()
        {
            try { return string.Equals(Environment.GetEnvironmentVariable("INKEYS_PPT_PERF"), "1", StringComparison.Ordinal); }
            catch { return false; }
        }

        internal static PptPerfSpan Begin(string stage)
        {
            if (!Enabled) return default(PptPerfSpan);
            return BeginCore(stage, current.Operation ?? "Maintenance", current.Request, current.Session, current.Binding);
        }

        internal static PptPerfSpan Begin(string stage, SlideShowObservationStamp stamp)
        {
            if (!Enabled) return default(PptPerfSpan);
            return BeginCore(stage, current.Operation ?? "Maintenance", current.Request, stamp.Session, stamp.Binding);
        }

        internal static PptPerfSpan Begin(string stage, SlideShowExitRequest request)
        {
            if (!Enabled) return default(PptPerfSpan);
            return BeginCore(stage, request.Operation.ToString(), request.DiagnosticId,
                request.Session, request.HasStamp ? request.Stamp.Binding : 0);
        }

        internal static PptPerfSpan Begin(string stage, SlideShowOwnerOperation operation,
            long request, SlideShowObservationStamp stamp)
        {
            if (!Enabled) return default(PptPerfSpan);
            return BeginCore(stage, operation.ToString(), request, stamp.Session, stamp.Binding);
        }

        private static PptPerfSpan BeginCore(string stage, string operation, long request, long session, long binding)
        {
            try
            {
                long qpc = Stopwatch.GetTimestamp();
                Context previous = current;
                Context context = new Context { Span = Interlocked.Increment(ref sequence),
                    Operation = operation, Request = request, Session = session, Binding = binding };
                current = context;
                Emit("begin", stage, context, previous.Span, qpc, -2, -1, -1, -1);
                return new PptPerfSpan(stage, context, previous);
            }
            catch { return default(PptPerfSpan); }
        }

        internal static long Timestamp() { return Enabled ? Stopwatch.GetTimestamp() : 0; }

        internal static void Instant(string stage, SlideShowExitRequest request, long qpc, int result = -2)
        {
            if (qpc == 0) return;
            try
            {
                Context context = new Context { Span = Interlocked.Increment(ref sequence),
                    Operation = request.Operation.ToString(), Request = request.DiagnosticId,
                    Session = request.Session, Binding = request.HasStamp ? request.Stamp.Binding : 0 };
                Emit("instant", stage, context, current.Span, qpc, result,
                    request.HasStarted ? 1 : 0, request.IsCompleted ? 1 : 0, request.IsCancelled ? 1 : 0);
            }
            catch { }
        }

        internal static void End(string stage, Context context, Context previous,
            int result, int executing, int completed, int cancelled)
        {
            try
            {
                long qpc = Stopwatch.GetTimestamp();
                if (current.Span == context.Span) current = previous;
                Emit("end", stage, context, previous.Span, qpc, result, executing, completed, cancelled);
            }
            catch { }
        }

        private static void Emit(string kind, string stage, Context context, long parent,
            long qpc, int result, int executing, int completed, int cancelled)
        {
            try
            {
                uint thread = GetCurrentThreadId();
                string line = string.Format(CultureInfo.InvariantCulture,
                    "[PptPerf] v=1 source=managed pid={0} tid={1} qpc={2} frequency={3} span={4} parent={5} event={6} stage={7} op={8} request={9} show_session={10} binding={11} result={12} executing={13} completed={14} cancelled={15}",
                    processId, thread, qpc, Stopwatch.Frequency, context.Span, parent, kind, stage,
                    context.Operation, context.Request, context.Session, context.Binding,
                    result, executing, completed, cancelled);
                lock (outputGate)
                {
                    TextWriter writer = testEnabled >= 0 ? testOutput : GetOutput();
                    if (writer != null) writer.WriteLine(line);
                }
            }
            catch { } // 输出故障不能覆盖业务异常、IErrorInfo 或中断 finally 释放。
        }

        private static TextWriter GetOutput()
        {
            if (!outputOpened)
            {
                outputOpened = true;
                try { output = new StreamWriter(Console.OpenStandardError(), new UTF8Encoding(false)) { AutoFlush = true }; }
                catch { output = null; }
            }
            return output;
        }

        // 测试只替换诊断出口，不替换真实 COM 访问或调度。
        internal static IDisposable UseTestOutput(bool enabled, TextWriter writer)
        {
            lock (outputGate)
            {
                var lease = new TestOutputLease(testEnabled, testOutput);
                testOutput = writer;
                Thread.VolatileWrite(ref testEnabled, enabled ? 1 : 0);
                return lease;
            }
        }

        private sealed class TestOutputLease : IDisposable
        {
            private readonly int previousEnabled;
            private readonly TextWriter previousOutput;
            internal TestOutputLease(int enabled, TextWriter writer) { previousEnabled = enabled; previousOutput = writer; }
            public void Dispose()
            {
                lock (outputGate)
                {
                    testOutput = previousOutput;
                    Thread.VolatileWrite(ref testEnabled, previousEnabled);
                }
            }
        }
    }

    internal struct PptPerfSpan : IDisposable
    {
        private readonly string stage;
        private readonly PptPerfTrace.Context context, previous;
        private int result, executing, completed, cancelled;
        private bool disposed;
        internal PptPerfSpan(string stage, PptPerfTrace.Context context, PptPerfTrace.Context previous)
        {
            this.stage = stage; this.context = context; this.previous = previous;
            result = -2; executing = completed = cancelled = -1; disposed = false;
        }
        internal void SetResult(int value) { result = value; }
        internal void SetRequestState(SlideShowExitRequest request)
        {
            if (context.Span == 0) return;
            executing = request.HasStarted ? 1 : 0;
            completed = request.IsCompleted ? 1 : 0;
            cancelled = request.IsCancelled ? 1 : 0;
        }
        public void Dispose()
        {
            if (disposed || context.Span == 0) return;
            disposed = true;
            PptPerfTrace.End(stage, context, previous, result, executing, completed, cancelled);
        }
    }
}
