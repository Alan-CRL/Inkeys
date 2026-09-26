using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Threading;

namespace PptCOM
{
    internal struct SlideShowObservationStamp
    {
        public long Generation;
        public long Session;
        public long Binding;
        public string Lifecycle;
        public long Window;
    }

    // 事件只推进轻量代次；JSON 由既有 service owner 构造，getter 永不访问 Office。
    internal sealed class SlideShowSessionCache
    {
        private readonly object gate = new object();
        private long generation;
        private long session;
        private long binding;
        private bool knownActive;
        private bool beginPending;
        private string lifecycle = "Unknown";
        private long window;
        private long stateRevision;
        private string fingerprint;
        private string serialized = string.Empty;

        public SlideShowSessionCache()
        {
            Publish(Capture(), PresentationDescriptorValue.CreateStatus("Unavailable", 0), "Unknown");
        }

        public SlideShowObservationStamp Begin(long bindingRevision, bool ready = true)
        {
            lock (gate)
            {
                binding = bindingRevision;
                ++session;
                ++generation;
                knownActive = true;
                beginPending = !ready;
                lifecycle = ready ? "Active" : "Unknown";
                window = 0;
                return Capture();
            }
        }

        public void FinishBegin(SlideShowObservationStamp stamp)
        {
            lock (gate)
            {
                // 同场回调内可能又有页事件；按 session 校验，不把它误当新一场或永久留在 pending。
                if (!beginPending || !knownActive || stamp.Session != session || stamp.Binding != binding) return;
                beginPending = false;
                lifecycle = "Active";
                ++generation;
            }
        }

        public void Observe(long bindingRevision, bool? active, long hwnd)
        {
            lock (gate)
            {
                if (binding != bindingRevision)
                {
                    binding = bindingRevision;
                    knownActive = false;
                    beginPending = false;
                    window = 0;
                    ++generation;
                }
                string next = active.HasValue ? (active.Value ? "Active" : "Inactive") : "Unknown";
                if (active == true)
                {
                    if (!knownActive || (window != 0 && hwnd != 0 && window != hwnd))
                    {
                        ++session;
                        ++generation;
                    }
                    knownActive = true;
                    if (hwnd != 0 && window != hwnd)
                    {
                        window = hwnd;
                        ++generation;
                    }
                }
                else if (active == false)
                {
                    knownActive = false;
                    beginPending = false;
                    window = 0;
                }
                if (lifecycle != next)
                {
                    lifecycle = next;
                    ++generation;
                }
            }
        }

        public void InvalidateBinding(long bindingRevision)
        {
            lock (gate)
            {
                // 已收到真正 End 后的清理不能用 Unknown 覆盖它，避免 native 采样错过退出边沿。
                Observe(bindingRevision, lifecycle == "Inactive" ? (bool?)false : null, 0);
            }
        }

        public bool ObserveIfCurrent(SlideShowObservationStamp stamp,
            long bindingRevision, bool? active, long hwnd)
        {
            lock (gate)
            {
                if (beginPending || stamp.Generation != generation || stamp.Binding != binding) return false;
                Observe(bindingRevision, active, hwnd);
                return true;
            }
        }

        public void PageChanged()
        {
            lock (gate) { ++generation; }
        }

        public SlideShowObservationStamp Capture()
        {
            lock (gate)
            {
                return new SlideShowObservationStamp
                {
                    Generation = generation, Session = session, Binding = binding,
                    Lifecycle = lifecycle, Window = window
                };
            }
        }

        public bool Matches(long expectedSession)
        {
            lock (gate)
            {
                return expectedSession > 0 && session == expectedSession &&
                    knownActive && lifecycle == "Active";
            }
        }

        public bool IsCurrent(SlideShowObservationStamp stamp)
        {
            lock (gate) { return stamp.Generation == generation && stamp.Binding == binding; }
        }

        public bool Publish(SlideShowObservationStamp stamp,
            PresentationDescriptorValue descriptor, string pageStatus)
        {
            if (descriptor == null || descriptor.bindingRevision != stamp.Binding) return false;
            if (stamp.Lifecycle != "Active")
            {
                descriptor = PresentationDescriptorValue.CreateStatus("Unavailable", stamp.Binding);
                pageStatus = "Unknown";
            }
            string descriptorJson = PresentationDescriptorJson.Serialize(descriptor);
            lock (gate)
            {
                // COM 读取期间发生 Begin/End/换页时，旧结果不能复活旧场次或旧页。
                if (stamp.Generation != generation || stamp.Binding != binding) return false;
                if (stamp.Lifecycle == "Active" && pageStatus != "Unknown" && descriptor.slideShowHwnd != 0)
                {
                    // WPS 的 HWND 也用相同 late-bound descriptor，避免依赖 PIA cast 才能识别换窗。
                    if (window != 0 && window != descriptor.slideShowHwnd)
                    {
                        ++session;
                        ++generation;
                        stamp.Session = session;
                    }
                    window = descriptor.slideShowHwnd;
                }
                string content = "\"showSessionRevision\":" + stamp.Session.ToString(CultureInfo.InvariantCulture) +
                    ",\"bindingRevision\":" + stamp.Binding.ToString(CultureInfo.InvariantCulture) +
                    ",\"lifecycle\":\"" + stamp.Lifecycle + "\",\"pageStatus\":\"" + pageStatus +
                    "\",\"descriptor\":" + descriptorJson + "}";
                if (fingerprint == content) return true;
                ++stateRevision;
                serialized = "{\"schemaVersion\":1,\"stateRevision\":" +
                    stateRevision.ToString(CultureInfo.InvariantCulture) + "," + content;
                fingerprint = content;
                return true;
            }
        }

        public string GetSince(long afterRevision)
        {
            lock (gate) { return afterRevision == stateRevision ? string.Empty : serialized; }
        }
    }

    internal sealed class SlideShowExitRequest
    {
        private readonly object gate = new object();
        private bool cancelled;
        private bool completed;
        private int result;
        public readonly long Session;

        public SlideShowExitRequest(long session) { Session = session; }

        public bool IsCancelled { get { lock (gate) { return cancelled; } } }

        public int Wait(int milliseconds)
        {
            Stopwatch timer = Stopwatch.StartNew();
            lock (gate)
            {
                while (!completed)
                {
                    int remaining = milliseconds - (int)Math.Min(int.MaxValue, timer.ElapsedMilliseconds);
                    if (remaining <= 0)
                    {
                        cancelled = true;
                        return -1;
                    }
                    Monitor.Wait(gate, remaining);
                }
                return result;
            }
        }

        public void Complete(int value)
        {
            lock (gate)
            {
                result = value;
                completed = true;
                Monitor.PulseAll(gate);
            }
        }
    }

    // 复用唯一 owner：事件唤醒不会提前运行 ROT；超时请求留在队列中也不能稍后执行。
    internal sealed class SlideShowOwnerMailbox
    {
        private readonly AutoResetEvent wake = new AutoResetEvent(false);
        private readonly object gate = new object();
        private readonly Queue<SlideShowExitRequest> exits = new Queue<SlideShowExitRequest>();
        private long nextMaintenance;
        private bool running;

        public void Start()
        {
            lock (gate) { running = true; nextMaintenance = 0; }
        }

        public void Stop()
        {
            lock (gate)
            {
                running = false;
                while (exits.Count != 0) exits.Dequeue().Complete(-1);
            }
            wake.Set();
        }

        public void Wake() { wake.Set(); }

        public void MaintenanceCompleted()
        {
            nextMaintenance = Stopwatch.GetTimestamp() + Stopwatch.Frequency / 2;
        }

        public int MaintenanceWaitMilliseconds
        {
            get
            {
                long remaining = nextMaintenance - Stopwatch.GetTimestamp();
                return remaining <= 0 ? 0 : (int)Math.Min(500,
                    Math.Ceiling(remaining * 1000.0 / Stopwatch.Frequency));
            }
        }

        public bool Wait(int milliseconds) { return wake.WaitOne(milliseconds); }

        public int RequestExit(long session, int milliseconds)
        {
            SlideShowExitRequest request = new SlideShowExitRequest(session);
            lock (gate)
            {
                if (!running || exits.Count >= 8) return -1;
                exits.Enqueue(request);
            }
            wake.Set();
            return request.Wait(milliseconds);
        }

        public void ProcessExits(Func<SlideShowExitRequest, int> execute)
        {
            while (true)
            {
                SlideShowExitRequest request;
                lock (gate)
                {
                    if (exits.Count == 0) return;
                    request = exits.Dequeue();
                }
                int result = -1;
                try { if (!request.IsCancelled) result = execute(request); }
                catch { result = -1; }
                finally { request.Complete(result); }
            }
        }
    }

    internal static class SlideShowSessionExit
    {
        public static int Execute(SlideShowExitRequest request, SlideShowSessionCache state,
            object window, ILateBoundComAccessor accessor)
        {
            if (request.IsCancelled || !state.Matches(request.Session) || window == null) return 0;
            SlideShowObservationStamp stamp = state.Capture();
            if (stamp.Window == 0) return 0;
            object view = null;
            object hwnd = null;
            try
            {
                hwnd = accessor.GetProperty(window, "HWND");
                if (accessor.IsComObject(hwnd)) return -1;
                long actualWindow = hwnd is IntPtr ? ((IntPtr)hwnd).ToInt64() :
                    Convert.ToInt64(hwnd, CultureInfo.InvariantCulture);
                if (actualWindow != stamp.Window) return 0;
                view = accessor.GetProperty(window, "View");
                // 获取 View 可能泵送 Begin/End；调用前再校验，且绝不重新读取新场次窗口。
                if (request.IsCancelled || !state.IsCurrent(stamp) || !state.Matches(request.Session)) return 0;
                accessor.InvokeMethod(view, "Exit");
                return 1;
            }
            catch { return -1; }
            finally
            {
                accessor.Release(view);
                if (accessor.IsComObject(hwnd)) accessor.Release(hwnd);
            }
        }
    }
}
