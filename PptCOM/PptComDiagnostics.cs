using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

namespace PptCOM
{
    // 只记录已有取得；弱键及纯值账本不得把 RCW 保活，也不得为日志查询 COM 身份。
    internal static class PptComDiagnostics
    {
        private sealed class Entry
        {
            internal long Id, Acquired, Released;
            internal string Source;
        }

        private struct OfficeEvent
        {
            internal long Binding, CurrentBinding, Qpc;
            internal uint Thread;
            internal int Kind;
        }
        private static readonly ConcurrentQueue<OfficeEvent> officeEvents = new ConcurrentQueue<OfficeEvent>();
        private static int officeEventCount;
        private static long droppedOfficeEvents;

        private static readonly ConditionalWeakTable<object, Entry> identities = new ConditionalWeakTable<object, Entry>();
        private static readonly Dictionary<long, Entry> outstanding = new Dictionary<long, Entry>();
        private static readonly object gate = new object(), outputGate = new object();
        private static long nextId, sequence, acquired, released, untracked, failures, dropped, outputFailures;
        private static int enabled;
        private static readonly uint processId = GetCurrentProcessId();
        [DllImport("kernel32.dll")] private static extern uint GetCurrentProcessId();
        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();

        internal static bool Enabled { get { return Thread.VolatileRead(ref enabled) != 0; } }
        internal static void SetEnabled(bool value) { Thread.VolatileWrite(ref enabled, value ? 1 : 0); }

        private static Entry CreateEntry(object value)
        { return new Entry { Id = Interlocked.Increment(ref nextId), Source = "untracked" }; }

        internal static long Reference(object value)
        {
            if (!Enabled || value == null) return 0;
            try { return identities.GetValue(value, CreateEntry).Id; }
            catch { return 0; }
        }

        private static long Write(string kind, string detail)
        {
            if (!Enabled) return 0;
            long id = Interlocked.Increment(ref sequence);
            try
            {
                lock (outputGate)
                    Console.WriteLine("[PptComLife] v=1 seq=" + id + " pid=" + processId +
                        " tid=" + GetCurrentThreadId() + " qpc=" + Stopwatch.GetTimestamp() +
                        " event=" + kind + " " + detail);
            }
            catch { Interlocked.Increment(ref outputFailures); }
            return id;
        }

        internal static void Event(string kind, string detail)
        { if (Enabled) Write(kind, detail); }

        internal static void QueueOfficeEvent(long binding, long currentBinding, int kind)
        {
            if (!Enabled) return;
            if (Interlocked.Increment(ref officeEventCount) > 64)
            { Interlocked.Decrement(ref officeEventCount); Interlocked.Increment(ref droppedOfficeEvents); return; }
            try
            {
                // COM 回调只排入纯值；控制台锁和写入不得阻塞 Office 的关闭通知。
                officeEvents.Enqueue(new OfficeEvent { Binding = binding, CurrentBinding = currentBinding,
                    Kind = kind, Qpc = Stopwatch.GetTimestamp(), Thread = GetCurrentThreadId() });
            }
            catch { Interlocked.Decrement(ref officeEventCount); Interlocked.Increment(ref droppedOfficeEvents); }
        }

        internal static void FlushOfficeEvents()
        {
            try
            {
                OfficeEvent item;
                int drained = 0;
                while (drained++ < 64 && officeEvents.TryDequeue(out item))
                {
                    Interlocked.Decrement(ref officeEventCount);
                    Event("office.event", "binding=" + item.Binding + " current_binding=" + item.CurrentBinding +
                        " dispid=" + item.Kind + " event_qpc=" + item.Qpc + " event_tid=" + item.Thread);
                }
            }
            catch { }
        }

        private static string Timing(long ticks)
        { return ticks < 0 ? string.Empty : " elapsed_ticks=" + ticks + " frequency=" + Stopwatch.Frequency; }

        internal static long BeginCall(object target, string member)
        {
            if (!Enabled) return 0;
            try { return Write("call.begin", "target=" + Reference(target) + " member=" + member); }
            catch { return 0; }
        }

        internal static void CallFailed(long call, object target, string member, Exception error, long elapsedTicks = -1)
        {
            if (!Enabled) return;
            try { Write("call.fail", "call=" + call + " target=" + Reference(target) +
                " member=" + member + " hr=0x" + ComFailure.ErrorCode(error).ToString("X8") +
                " exception=" + error.GetType().Name + Timing(elapsedTicks)); }
            catch { }
        }

        internal static object Returned(long call, object target, string member, object value, long elapsedTicks = -1)
        {
            if (!Enabled) return value;
            try
            {
                if (value != null && Marshal.IsComObject(value)) Acquired(value, member, target, call, elapsedTicks);
                else
                {
                    string scalar = value == null ? "null" : "scalar";
                    if (value is int || value is long || value is bool || value is IntPtr)
                        scalar = Convert.ToString(value, CultureInfo.InvariantCulture);
                    Write("call.end", "call=" + call + " target=" + Reference(target) +
                        " member=" + member + " value=" + scalar + Timing(elapsedTicks));
                }
            }
            catch { }
            return value;
        }

        internal static void Acquired(object value, string source, object parent = null, long call = 0, long elapsedTicks = -1)
        {
            if (!Enabled || value == null) return;
            try
            {
                if (!Marshal.IsComObject(value)) return;
                Entry entry = identities.GetValue(value, CreateEntry);
                string detail;
                lock (gate)
                {
                    ++entry.Acquired; ++acquired; entry.Source = source;
                    if (outstanding.ContainsKey(entry.Id) || outstanding.Count < 4096) outstanding[entry.Id] = entry;
                    else ++dropped;
                    detail = "call=" + call + " obj=" + entry.Id + " parent=" + Reference(parent) +
                        " source=" + source + " acquired=" + entry.Acquired + " released=" + entry.Released +
                        " balance=" + (entry.Acquired - entry.Released) + Timing(elapsedTicks);
                }
                Write("acquire", detail);
            }
            catch { }
        }

        internal static void Transfer(object value, string destination)
        {
            if (!Enabled || value == null) return;
            try
            {
                Entry entry = identities.GetValue(value, CreateEntry);
                lock (gate) entry.Source = destination;
                Write("transfer", "obj=" + entry.Id + " destination=" + destination);
            }
            catch { }
        }

        internal static int Release(object value, string source)
        {
            if (!Enabled) return Marshal.ReleaseComObject(value);
            long id = Reference(value);
            long call = Write("release.begin", "obj=" + id + " source=" + source);
            long started = Stopwatch.GetTimestamp();
            try
            {
                // 原调用只执行一次；remaining 是 RCW 计数，不是原生 IUnknown 的总引用数。
                int remaining = Marshal.ReleaseComObject(value);
                long elapsed = Stopwatch.GetTimestamp() - started;
                try
                {
                    Entry entry = identities.GetValue(value, CreateEntry);
                    string detail;
                    lock (gate)
                    {
                        bool counted = entry.Acquired > entry.Released;
                        if (counted) { ++entry.Released; ++released; } else ++untracked;
                        if (entry.Acquired == entry.Released) outstanding.Remove(entry.Id);
                        detail = "call=" + call + " obj=" + id + " source=" + source +
                            " counted=" + (counted ? 1 : 0) + " acquired=" + entry.Acquired +
                            " released=" + entry.Released + " balance=" + (entry.Acquired - entry.Released) +
                            " remaining=" + remaining + Timing(elapsed);
                    }
                    Write("release.end", detail);
                }
                catch { }
                return remaining;
            }
            catch (Exception error)
            {
                Interlocked.Increment(ref failures);
                CallFailed(call, value, "release:" + source, error, Stopwatch.GetTimestamp() - started);
                throw;
            }
        }

        internal static void Summary(string stage)
        {
            if (!Enabled) return;
            try
            {
                List<string> pending = new List<string>();
                string totals;
                lock (gate)
                {
                    totals = "stage=" + stage + " acquired=" + acquired + " released=" + released +
                        " balance=" + (acquired - released) + " outstanding=" + outstanding.Count +
                        " untracked_release=" + untracked + " release_failures=" + failures +
                        " dropped=" + dropped + " output_failures=" + outputFailures +
                        " event_dropped=" + Interlocked.Read(ref droppedOfficeEvents);
                    foreach (Entry entry in outstanding.Values)
                        pending.Add("stage=" + stage + " obj=" + entry.Id + " source=" + entry.Source +
                            " acquired=" + entry.Acquired + " released=" + entry.Released +
                            " balance=" + (entry.Acquired - entry.Released));
                }
                Write("summary", totals);
                foreach (string item in pending) Write("outstanding", item);
            }
            catch { }
        }
    }
}
