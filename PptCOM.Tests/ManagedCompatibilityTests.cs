using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Threading;
using System.Web.Script.Serialization;
using RotTable = System.Runtime.InteropServices.ComTypes.IRunningObjectTable;
using RotEnumerator = System.Runtime.InteropServices.ComTypes.IEnumMoniker;
using RotMoniker = System.Runtime.InteropServices.ComTypes.IMoniker;
using RotBind = System.Runtime.InteropServices.ComTypes.IBindCtx;
using RotStream = System.Runtime.InteropServices.ComTypes.IStream;
using RotFileTime = System.Runtime.InteropServices.ComTypes.FILETIME;

namespace PptCOM.Tests
{
    internal static class ManagedCompatibilityTests
    {
        private static int failures;
        private static void Check(bool value, string name)
        {
            if (value) return;
            ++failures;
            Console.Error.WriteLine("FAIL " + name);
        }

        private sealed class ServerHarness : IDisposable
        {
            public readonly FakeGraph Graph = new FakeGraph("WPP");
            public readonly FakeAccessor Accessor = new FakeAccessor();
            public readonly PptCOMServer Server;
            public readonly SlideShowSessionCache State;
            private readonly IntPtr slots = Marshal.AllocHGlobal(12);
            public readonly FakeNode Windows = new FakeNode("windows");
            public readonly FakeNode Navigation = new FakeNode("navigation");

            public ServerHarness() : this(null, null) { }
            public ServerHarness(PptCOMServer.AcquireRot acquireRot, Action<object> releaseRot)
            {
                // 默认 owner 测试不枚举用户的 ROT；专用扫描测试才注入本地夹具。
                Server = new PptCOMServer(Accessor,
                    acquireRot ?? new PptCOMServer.AcquireRot(NoTestRot), releaseRot);
                State = (SlideShowSessionCache)Get("slideShowSession");
                Set("presentationBindingRevision", 1L);
                // 测试只给生产 unsafe 槽分配自有内存；不改工程 unsafe 配置、不启动 Office。
                SetPointer("pptTotalPage", slots);
                SetPointer("pptCurrentPage", new IntPtr(slots.ToInt64() + 4));
                SetPointer("offSignal", new IntPtr(slots.ToInt64() + 8));
                Marshal.WriteInt32(slots, 0, -1);
                Marshal.WriteInt32(slots, 4, -1);
                Marshal.WriteInt32(slots, 8, 0);
                Graph.Application.Properties["ActivePresentation"] = Graph.Presentation;
                Graph.Application.Properties["SlideShowWindows"] = Windows;
                Windows.Properties["Count"] = 1;
                Graph.Presentation.Properties["SlideShowWindow"] = Graph.Window;
                Graph.Window.Properties["Presentation"] = Graph.Presentation;
                Graph.Window.Properties["SlideNavigation"] = Navigation;
                Graph.Window.Properties["Active"] = -1;
                ((FakeNode)Graph.Window.Properties["View"]).Properties["State"] = 1;
                FakeNode holder = new FakeNode("holder");
                holder.Properties["Application"] = Graph.Application;
                holder.Properties["Presentation"] = Graph.Presentation;
                holder.Properties["Window"] = Graph.Window;
                Set("pptApplication", Accessor.GetProperty(holder, "Application"));
                Set("pptActivePresentation", Accessor.GetProperty(holder, "Presentation"));
                Set("pptSlideShowWindow", Accessor.GetProperty(holder, "Window"));
                State.Observe(1, true, 100);
            }

            private static int NoTestRot(out RotTable table)
            {
                table = null;
                return unchecked((int)0x80004005);
            }

            private void SetPointer(string name, IntPtr value)
            {
                var field = typeof(PptCOMServer).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic);
                var setter = new DynamicMethod("SetTestSlot", typeof(void),
                    new[] { typeof(PptCOMServer), typeof(IntPtr) }, typeof(PptCOMServer), true);
                ILGenerator il = setter.GetILGenerator();
                il.Emit(OpCodes.Ldarg_0); il.Emit(OpCodes.Ldarg_1); il.Emit(OpCodes.Conv_U);
                il.Emit(OpCodes.Stfld, field); il.Emit(OpCodes.Ret);
                ((Action<PptCOMServer, IntPtr>)setter.CreateDelegate(typeof(Action<PptCOMServer, IntPtr>)))(Server, value);
            }
            public object Get(string name)
            { return typeof(PptCOMServer).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).GetValue(Server); }
            public void Set(string name, object value)
            { typeof(PptCOMServer).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(Server, value); }
            public object Call(string name, params object[] arguments)
            { return typeof(PptCOMServer).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic).Invoke(Server, arguments); }
            public int Outstanding(string name)
            {
                int acquired;
                Accessor.Acquired.TryGetValue(name, out acquired);
                return acquired - Accessor.ReleaseCount(name);
            }
            public int Current { get { return Marshal.ReadInt32(slots, 4); } }
            public int Total { get { return Marshal.ReadInt32(slots, 0); } }
            public void Stop() { Marshal.WriteInt32(slots, 8, 1); }
            public bool Observe() { return (bool)Call("ObserveBoundShow"); }
            public int Command(SlideShowOwnerOperation operation, bool check = false)
            { return (int)Call("ExecuteOwnerCommand", new SlideShowExitRequest(State.Capture(), operation, check)); }
            public void Dispose()
            {
                Accessor.FailureStage = null;
                Accessor.AfterAcquire = null;
                Call("FullCleanup", false);
                Marshal.FreeHGlobal(slots);
                Check(Accessor.IsBalanced(), "owner cleanup releases all acquisitions without GC");
            }
        }

        private static Dictionary<string, object> Json(string value)
        { return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(value); }

        private static void TestOwnerPollingAndOwnership()
        {
            using (var h = new ServerHarness())
            {
                long original = h.State.Capture().Session;
                for (int index = 0; index < 10; ++index)
                    Check(h.Observe(), "same identity polling remains bound");
                Check(h.Outstanding("window") == 1 && h.Outstanding("presentation") == 1 &&
                    h.Outstanding("application") == 1 && h.Current == 2 && h.Total == 3,
                    "same identity new returns are released independently of roots");
                Check(h.State.Capture().Session == original, "same identity does not invent a show");
                h.Call("TryGetActivePresentationFullNameForCompare", h.Graph.Application);
                h.Call("TryGetActiveSlideShowHwndForCompare", h.Graph.Application, IntPtr.Zero);
                Check(h.Outstanding("window") == 1 && h.Outstanding("presentation") == 1,
                    "comparison helpers release equal-identity temporaries");

                var replacement = new FakeNode("replacement");
                foreach (var pair in h.Graph.Window.Properties) replacement.Properties[pair.Key] = pair.Value;
                replacement.Properties["HWND"] = 0L;
                h.Graph.Presentation.Properties["SlideShowWindow"] = replacement;
                Check(h.Observe() && ReferenceEquals(h.Get("pptSlideShowWindow"), replacement) &&
                    h.State.Capture().Session > original && h.Outstanding("window") == 0 &&
                    h.Outstanding("replacement") == 1,
                    "new COM show replaces readable old Active even with HWND0");
                Check(h.State.Capture().Window == 0, "new show cannot inherit old HWND");
                long replacedSession = h.State.Capture().Session;
                h.Windows.Properties["Count"] = 0;
                Check(h.Observe() && h.State.Capture().Lifecycle == "Inactive" &&
                    h.Get("pptSlideShowWindow") == null && h.Current == -1 &&
                    h.Outstanding("replacement") == 0,
                    "confirmed Count0 clears owned window and raw page");
                h.Windows.Properties["Count"] = 1;
                Check(h.Observe() && h.State.Capture().Session > replacedSession &&
                    h.State.Capture().Lifecycle == "Active", "poll-only reopen creates a fresh epoch");
            }
            using (var h = new ServerHarness())
            {
                h.Graph.Application.Properties.Remove("SlideShowWindows");
                Check(h.Observe() && h.Current == 2 && h.Total == 3 && h.State.Capture().Lifecycle == "Active",
                    "missing Count uses fresh show Presentation and View evidence");
                h.Graph.Window.Properties["Presentation"] = new FakeNode("wrongPresentation");
                Check(h.Observe() && h.State.Capture().Lifecycle == "Unknown",
                    "missing Count cannot infer Active from wrong show Presentation");
            }
        }

        private static void TestBusyRetryAndMetadata()
        {
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check((string)Json(h.Server.GetPresentationDescriptor())["status"] == "StableSlideIds",
                    "initial production owner publishes stable graph");
                var current = (FakeNode)((FakeNode)h.Graph.Window.Properties["View"]).Properties["Slide"];
                current.Properties["SlideID"] = 999; // 先使 fast 失效，才能到达本例的完整拓扑 busy 注入点。
                h.Accessor.FailureStage = "slides.Item:2";
                h.Accessor.Failure = new COMException("busy", unchecked((int)0x8001010A));
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                var envelope = Json(h.Server.GetSlideShowState(0));
                Check((string)Json(h.Server.GetPresentationDescriptor())["status"] == "TransientBusy" &&
                    (string)envelope["pageStatus"] == "Unknown" &&
                    (string)((Dictionary<string, object>)envelope["descriptor"])["status"] == "TransientBusy",
                    "legacy and session publish the same busy evidence without reviving old Valid");
                DateTime retry = (DateTime)h.Get("presentationDescriptorRetryUtc");
                Check(retry <= DateTime.UtcNow.AddMilliseconds(500) &&
                    (DateTime)h.Get("nextPresentationDescriptorRefreshUtc") > retry,
                    "busy retry is independent of stable three-second audit");
                h.Accessor.FailureStage = null;
                current.Properties["SlideID"] = 202;
                h.Set("presentationDescriptorRetryUtc", DateTime.UtcNow.AddMilliseconds(-1));
                h.Call("RefreshPresentationDescriptorIfNeeded", false);
                Check((string)Json(h.Server.GetSlideShowState(0))["pageStatus"] == "Valid",
                    "nonforced recovery executes before stable deadline");
                h.State.Observe(1, false, 0);
                h.Set("presentationDescriptorRetryUtc", DateTime.MinValue);
                h.Call("RefreshPresentationDescriptorIfNeeded", false);
                Check((DateTime)h.Get("presentationDescriptorRetryUtc") == DateTime.MaxValue,
                    "inactive cancels busy deadline instead of empty one-millisecond spinning");
            }
            using (var h = new ServerHarness())
            {
                h.Graph.Presentation.Properties.Remove("Name");
                h.Graph.Presentation.Properties.Remove("FullName");
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                var envelope = Json(h.Server.GetSlideShowState(0));
                var descriptor = (Dictionary<string, object>)envelope["descriptor"];
                Check((string)descriptor["status"] == "Unavailable" && (string)envelope["pageStatus"] == "Unknown" &&
                    Convert.ToInt32(descriptor["currentPage"]) == 2 && Convert.ToInt32(descriptor["totalPage"]) == 3,
                    "missing optional metadata preserves fresh basic pages without granting ink identity");
            }
        }

        private static void TestOwnerCommandsAndCallbacks()
        {
            using (var h = new ServerHarness())
            {
                Check(h.Command(SlideShowOwnerOperation.Next) == 1 &&
                    h.Command(SlideShowOwnerOperation.Previous) == 1 &&
                    h.Command(SlideShowOwnerOperation.View) == 1 &&
                    h.Outstanding("view") == 0 && h.Outstanding("current") == 0 &&
                    h.Outstanding("navigation") == 0,
                    "owner commands release View/SlideNavigation temporaries");
                foreach (string stage in new[] { "window.View", "view.Previous", "window.SlideNavigation", "navigation.Visible" })
                {
                    h.Accessor.FailureStage = stage;
                    h.Accessor.Failure = new COMException("failed");
                    h.Command(stage.IndexOf("Navigation", StringComparison.OrdinalIgnoreCase) >= 0 ?
                        SlideShowOwnerOperation.View : SlideShowOwnerOperation.Previous);
                    Check(h.Outstanding("view") == 0 && h.Outstanding("navigation") == 0,
                        stage + " command failure balances temporaries");
                }
                h.Accessor.FailureStage = "view.Slide";
                h.Set("polling", 2);
                int before = h.Accessor.Invoked.Count;
                Check(h.Command(SlideShowOwnerOperation.Next, false) == 0 && h.Accessor.Invoked.Count == before,
                    "unchecked Next is still blocked on end screen");
                Check(h.Command(SlideShowOwnerOperation.Next, true) == 1,
                    "checked Next with polling2 preserves end-screen advance");
                h.Set("polling", 1);
                Check(h.Command(SlideShowOwnerOperation.Next, true) == 0,
                    "polling1 still requires a readable current page before advancing");
                h.Accessor.FailureStage = null;
                long beforeRetiredEvent = h.State.Capture().Generation;
                h.Call("DispatchBoundEvent", 0L, 2011);
                h.Call("DispatchBoundEvent", 0L, 2014);
                Check(h.State.Capture().Generation == beforeRetiredEvent,
                    "callbacks from retired binding cannot publish Begin or End to new binding");
                var request = new SlideShowExitRequest(h.State.Capture(), SlideShowOwnerOperation.Previous, false);
                h.State.PageChanged();
                Check((int)h.Call("ExecuteOwnerCommand", request) == 0, "queued stale generation is rejected");
                int windowAcquisitions = h.Outstanding("window");
                long beforeHintSession = h.State.Capture().Session;
                h.Call("SlideShowBegin", new FakeNode("eventBorrow"));
                h.Call("SlideShowShowEnd", h.Graph.Presentation);
                Check(ReferenceEquals(h.Get("pptSlideShowWindow"), h.Graph.Window) &&
                    h.Outstanding("window") == windowAcquisitions,
                    "Begin callback neither takes borrowed parameter ownership nor replaces root");
                Check(h.Observe() && h.State.Capture().Lifecycle == "Active" &&
                    h.State.Capture().Session == beforeHintSession,
                    "late same-binding Begin/End hints cannot restart or end the observed COM show");
                h.Call("BindEvents");
                Check((bool)h.Get("forcePolling") && !(bool)h.Get("bindingEvents"),
                    "unavailable PIA event wrapper leaves independent polling enabled");
            }
        }

        private static void TestMailboxExecution()
        {
            var mailbox = new SlideShowOwnerMailbox();
            mailbox.Start();
            var entered = new ManualResetEvent(false);
            var release = new ManualResetEvent(false);
            int result = 0;
            SlideShowExitRequest executing = null;
            Thread client = new Thread(() => { result = mailbox.RequestExit(1, 200); });
            client.Start();
            Check(mailbox.Wait(1000), "in-flight test request reaches mailbox");
            Thread owner = new Thread(() => mailbox.ProcessExits(request =>
            { executing = request; entered.Set(); release.WaitOne(); return 1; }));
            owner.Start();
            Check(entered.WaitOne(1000) && client.Join(2000) && result == -1 && !executing.IsCancelled,
                "timeout cannot claim cancellation after a COM operation has started");
            release.Set();
            Check(owner.Join(2000), "executing operation drains before owner cleanup");
            mailbox.Stop();
            entered.Close(); release.Close();
        }

        private static void TestEventCleanupOwnership()
        {
            using (var h = new ServerHarness())
            {
                var point = new FakeNode("connectionPoint");
                h.Graph.Application.Properties["ConnectionPoint"] = point;
                object acquiredPoint = h.Accessor.GetProperty(h.Graph.Application, "ConnectionPoint");
                int attempts = 0;
                var connection = new ComEventConnection(123, cookie =>
                {
                    Check(cookie == 123, "retry keeps the exact Advise cookie");
                    if (++attempts == 1) throw new COMException("busy", unchecked((int)0x8001010A));
                    return 0;
                }, () => h.Accessor.Release(acquiredPoint), delegate { });
                h.Set("eventConnection", connection);
                h.Call("FullCleanup", false);
                Check(ComEventRetirementRegistry.PendingCount == 1 && h.Outstanding("connectionPoint") == 1 &&
                    h.Outstanding("application") == 0 && h.Outstanding("presentation") == 0,
                    "failed Unadvise transfers independent CP to retired registry while roots release exactly once");
                using (var nextOwner = new ServerHarness())
                    nextOwner.Call("RetryRetiredEventConnections");
                Check(ComEventRetirementRegistry.PendingCount == 0 && attempts == 2 &&
                    h.Outstanding("connectionPoint") == 0,
                    "new server owner retries retired CP after original instance reset");
                Check(connection.TryDisconnect() && h.Outstanding("connectionPoint") == 0,
                    "completed disconnect is idempotent without duplicate CP release");
            }
            using (var h = new ServerHarness())
            {
                int attempts = 0, released = 0;
                bool allow = false;
                var connection = new ComEventConnection(321,
                    cookie => { ++attempts; return allow ? 0 : unchecked((int)0x80004005); },
                    () => ++released, delegate { });
                h.Set("eventConnection", connection);
                h.Stop();
                Check(h.Server.PptComService() == -1 && attempts == 4 && released == 0 &&
                    ComEventRetirementRegistry.PendingCount == 1,
                    "stop makes bounded explicit unbind retries and reports pending lease instead of false success");
                allow = true;
                h.Call("RetryRetiredEventConnections");
                Check(released == 1 && ComEventRetirementRegistry.PendingCount == 0,
                    "permanent-stop failure remains explicitly recoverable without GC");
            }
            int terminalReleases = 0;
            foreach (int error in new[] {
                unchecked((int)0x80040200), unchecked((int)0x80010108), unchecked((int)0x800401FD),
                unchecked((int)0x80010007), unchecked((int)0x80010012) })
            {
                int before = terminalReleases;
                var terminal = new ComEventConnection(1, cookie => error,
                    () => ++terminalReleases, delegate { });
                Check(terminal.TryDisconnect() && terminal.TryDisconnect() && terminalReleases == before + 1,
                    "terminal HRESULT 0x" + error.ToString("X8") + " releases CP acquisition exactly once");
            }
        }

        private static void TestDisconnectedAndLastDocumentClose()
        {
            foreach (Exception error in new Exception[] {
                new COMException("disconnected", unchecked((int)0x80010108)),
                new COMException("server died", unchecked((int)0x80010007)),
                new COMException("server died before execution", unchecked((int)0x80010012)),
                new InvalidComObjectException() })
            {
                using (var h = new ServerHarness())
                {
                    h.Accessor.FailureStage = "application.ActivePresentation";
                    h.Accessor.Failure = error;
                    Check(!h.Observe() && h.State.Capture().Lifecycle == "Inactive",
                        "disconnected root requests cleanup instead of indefinite Unknown");
                    h.Call("FullCleanup", false);
                    Check(h.Outstanding("application") == 0 && h.Outstanding("presentation") == 0 &&
                        h.Outstanding("window") == 0, "disconnected cleanup drains roots");
                }
            }
            using (var h = new ServerHarness())
            {
                var presentations = new FakeNode("presentations");
                presentations.Properties["Count"] = 0;
                h.Graph.Application.Properties["Presentations"] = presentations;
                h.Accessor.FailureStage = "application.ActivePresentation";
                h.Accessor.Failure = new COMException("no active presentation", unchecked((int)0x80020009));
                Check(!h.Observe() && h.State.Capture().Lifecycle == "Inactive" &&
                    h.Outstanding("presentations") == 0,
                    "independent Presentations.Count0 confirms last document close and releases temporary collection");
                h.Call("FullCleanup", false);
                Check(h.Outstanding("application") == 0, "last document close releases held application without GC");
            }
            using (var h = new ServerHarness())
            {
                h.Accessor.FailureStage = "application.ActivePresentation";
                h.Accessor.Failure = new COMException("busy", unchecked((int)0x8001010A));
                Check(h.Observe() && h.State.Capture().Lifecycle == "Unknown" && h.Outstanding("application") == 1,
                    "busy is not misclassified as a disconnected application");
            }
        }

        private static void TestWindowEvidence()
        {
            var show = new SlideShowWindowEvidence { Handle = new IntPtr(100), ProcessId = 10,
                ClassName = "screenClass", Title = "Deck.pptx - PowerPoint" };
            var edit = new SlideShowWindowEvidence { Handle = new IntPtr(101), ProcessId = 10,
                ClassName = "PPTFrameClass", Title = show.Title };
            Check(SlideShowWindowEvidence.Select(new[] { edit }, 10, "Deck.pptx") == IntPtr.Zero,
                "unique title does not turn an editor into show evidence");
            Check(SlideShowWindowEvidence.Select(new[] { show, edit }, 10, "Deck.pptx") == show.Handle,
                "known show class requires independent process/document association");
            Check(SlideShowWindowEvidence.Select(new[] { show }, 11, "Deck.pptx") == IntPtr.Zero &&
                SlideShowWindowEvidence.Select(new[] { show }, 0, "Deck.pptx") == IntPtr.Zero &&
                SlideShowWindowEvidence.Select(new[] { show }, 10, "Other.pptx") == IntPtr.Zero,
                "wrong/unknown COM process or wrong document is rejected");
            var other = new SlideShowWindowEvidence { Handle = new IntPtr(102), ProcessId = 10,
                ClassName = "screenClass", Title = "Other.pptx" };
            Check(SlideShowWindowEvidence.Select(new[] { show, other }, 10, "Deck.pptx") == IntPtr.Zero,
                "multiple show windows remain ambiguous even with one matching title");
        }

        private sealed class ThrowingCleanupWriter : System.IO.TextWriter
        {
            public override System.Text.Encoding Encoding { get { return System.Text.Encoding.UTF8; } }
            public override void WriteLine(string value) { throw new System.IO.IOException("closed diagnostic output"); }
        }

        private static void TestCleanupSurvivesDiagnosticFailure()
        {
            using (var h = new ServerHarness())
            {
                var point = new FakeNode("cleanupDiagnosticPoint");
                h.Graph.Application.Properties["ConnectionPoint"] = point;
                object acquiredPoint = h.Accessor.GetProperty(h.Graph.Application, "ConnectionPoint");
                bool allow = false;
                var connection = new ComEventConnection(246,
                    cookie => allow ? 0 : unchecked((int)0x8001010A),
                    () => h.Accessor.Release(acquiredPoint), delegate { });
                h.Set("eventConnection", connection);
                System.IO.TextWriter previous = Console.Out;
                try
                {
                    Console.SetOut(new ThrowingCleanupWriter());
                    try { h.Call("FullCleanup", false); }
                    catch { Check(false, "closed diagnostics cannot interrupt production root cleanup"); }
                    Check(h.Outstanding("application") == 0 && h.Outstanding("presentation") == 0 &&
                        h.Outstanding("window") == 0,
                        "root acquisitions release even when both cleanup diagnostics throw");
                    try { h.Call("RetryRetiredEventConnections"); }
                    catch { Check(false, "closed diagnostics cannot escape production CP retry"); }
                    Check(ComEventRetirementRegistry.PendingCount == 1 &&
                        h.Outstanding("cleanupDiagnosticPoint") == 1,
                        "failed CP remains owned and queued before a throwing diagnostic");
                }
                finally
                {
                    Console.SetOut(previous);
                    allow = true;
                    h.Call("RetryRetiredEventConnections");
                    Check(connection.TryDisconnect(), "diagnostic failure lease remains explicitly releasable");
                }
                Check(ComEventRetirementRegistry.PendingCount == 0 &&
                    h.Outstanding("cleanupDiagnosticPoint") == 0,
                    "later successful owner retry consumes the CP acquisition exactly once without GC");
            }
        }

        [DllImport("oleaut32.dll")]
        private static extern int GetErrorInfo(uint reserved, out IntPtr errorInfo);
        [DllImport("oleaut32.dll")]
        private static extern int SetErrorInfo(uint reserved, IntPtr errorInfo);

        private static void TestDiagnosticPreservesErrorInfo()
        {
            IntPtr original = IntPtr.Zero, marker = IntPtr.Zero, observed = IntPtr.Zero;
            GetErrorInfo(0, out original);
            try
            {
                // 仅测试夹具用此 API 设置已知 IErrorInfo；生产日志绝不能调用它。
                Marshal.GetHRForException(new InvalidOperationException("diagnostic marker"));
                Check(GetErrorInfo(0, out marker) == 0 && marker != IntPtr.Zero, "ErrorInfo fixture installs marker");
                SetErrorInfo(0, marker);
                Check(ComFailure.ErrorCode(new COMException("busy", unchecked((int)0x8001010A))) == unchecked((int)0x8001010A) &&
                    ComFailure.ErrorCode(new InvalidCastException()) == 0,
                    "pure diagnostic preserves external HRESULT and explicitly reports unknown managed code");
                using (PptPerfTrace.UseTestOutput(true, new System.IO.StringWriter()))
                using (PptPerfTrace.Begin("error-info-test")) { }
                Check(GetErrorInfo(0, out observed) == 0 && observed == marker,
                    "diagnostic neither replaces nor clears existing thread IErrorInfo");
                if (observed != IntPtr.Zero) { Marshal.Release(observed); observed = IntPtr.Zero; }
                SetErrorInfo(0, marker);
                using (PptPerfTrace.UseTestOutput(true, new ThrowingCleanupWriter()))
                using (var cleanup = new ServerHarness()) cleanup.Call("FullCleanup", true);
                Check(GetErrorInfo(0, out observed) == 0 && observed == marker,
                    "cleanup stages and a throwing perf sink preserve thread IErrorInfo");
                if (observed != IntPtr.Zero) { Marshal.Release(observed); observed = IntPtr.Zero; }
                SetErrorInfo(0, IntPtr.Zero);
                ComFailure.ErrorCode(new InvalidCastException());
                Check(GetErrorInfo(0, out observed) == 1 && observed == IntPtr.Zero,
                    "diagnostic does not create thread IErrorInfo when none exists");
            }
            finally
            {
                // 恢复进入测试前的状态，所有原始 IErrorInfo 指针逐笔配平，不污染随后真实 COM cases。
                SetErrorInfo(0, original);
                if (observed != IntPtr.Zero) Marshal.Release(observed);
                if (marker != IntPtr.Zero) Marshal.Release(marker);
                if (original != IntPtr.Zero) Marshal.Release(original);
            }
        }

        private static FakeNode CurrentSlide(ServerHarness h)
        {
            return (FakeNode)((FakeNode)h.Graph.Window.Properties["View"]).Properties["Slide"];
        }

        private static FakeNode Slides(ServerHarness h)
        {
            return (FakeNode)h.Graph.Presentation.Properties["Slides"];
        }

        private static int ItemReadCount(ServerHarness h)
        {
            int count = 0;
            foreach (string operation in h.Accessor.Operations)
                if (operation.StartsWith("slides.Item:", StringComparison.Ordinal)) ++count;
            return count;
        }

        private static string DescriptorStatus(ServerHarness h)
        {
            return (string)Json(h.Server.GetPresentationDescriptor())["status"];
        }

        private static void TestFastAndPeriodicMaintenance()
        {
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 3 && DescriptorStatus(h) == "StableSlideIds",
                    "initial binding performs one complete topology validation");

                FakeNode current = CurrentSlide(h);
                current.Properties["SlideIndex"] = 3;
                current.Properties["SlideID"] = 303;
                h.State.PageChanged();
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                var descriptor = Json(h.Server.GetPresentationDescriptor());
                Check(ItemReadCount(h) == 0 && DescriptorStatus(h) == "StableSlideIds" &&
                    Convert.ToInt32(descriptor["currentPage"]) == 3 &&
                    Convert.ToInt32(descriptor["currentSlideId"]) == 303,
                    "ordinary same-binding navigation uses fast current-page validation with zero Items");

                h.Accessor.Operations.Clear();
                h.Set("presentationDescriptorRefreshPending", 0);
                h.Set("nextPresentationDescriptorRefreshUtc", DateTime.UtcNow.AddMilliseconds(-1));
                h.Call("RefreshPresentationDescriptorIfNeeded", false);
                Check(ItemReadCount(h) == 0 && DescriptorStatus(h) == "StableSlideIds",
                    "periodic deadline alone never authorizes a full document scan");

                var replacement = new FakeNode("fastReplacement");
                foreach (var pair in h.Graph.Window.Properties) replacement.Properties[pair.Key] = pair.Value;
                replacement.Properties["HWND"] = 0L;
                h.Graph.Presentation.Properties["SlideShowWindow"] = replacement;
                Check(h.Observe() && h.State.Capture().Window == 0,
                    "same-binding replacement establishes a fresh session without a window anchor");
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                descriptor = Json(h.Server.GetPresentationDescriptor());
                Check(ItemReadCount(h) == 3 && Convert.ToInt64(descriptor["slideShowHwnd"]) == 0 &&
                    Convert.ToInt32(descriptor["applicationProcessId"]) == 0,
                    "new session validates complete topology without inheriting an old HWND or process identity");
                Check(h.Accessor.IsBalanced() || h.Outstanding("application") == 1,
                    "fast/full interleaving keeps the ledger available for final owner cleanup");
            }
        }

        private static void TestFreshPresentationIdentity()
        {
            foreach (bool restart in new[] { false, true })
            {
                using (var h = new ServerHarness())
                {
                    h.Call("RefreshPresentationDescriptorIfNeeded", true);
                    long session = h.State.Capture().Session;
                    long binding = (long)h.Get("presentationBindingRevision");
                    if (restart)
                    {
                        h.Windows.Properties["Count"] = 0;
                        Check(h.Observe(), "same binding observes the end before SaveAs");
                    }
                    h.Graph.Presentation.Properties["FullName"] = @"C:\Lessons\Renamed.pptx";
                    h.Graph.Presentation.Properties["Name"] = "Renamed.pptx";
                    if (restart)
                    {
                        h.Windows.Properties["Count"] = 1;
                        Check(h.Observe() && h.State.Capture().Session > session,
                            "reopening the same presentation establishes a new show session");
                    }
                    h.Accessor.Operations.Clear();
                    h.Call("RefreshPresentationDescriptorIfNeeded", true);
                    var descriptor = Json(h.Server.GetPresentationDescriptor());
                    Check((string)descriptor["fullName"] == @"C:\Lessons\Renamed.pptx" &&
                        (string)descriptor["presentationName"] == "Renamed.pptx" &&
                        DescriptorStatus(h) == "StableSlideIds" && ItemReadCount(h) == 3 &&
                        (long)h.Get("presentationBindingRevision") == binding,
                        restart ? "same-binding SaveAs between shows cannot reuse the previous document identity" :
                            "same-session SaveAs refreshes metadata and validates topology without retiring event binding");
                    h.Accessor.Operations.Clear();
                    h.Call("RefreshPresentationDescriptorIfNeeded", true);
                    Check(ItemReadCount(h) == 0, "unchanged fresh identity returns to fast validation after SaveAs");
                }
            }
            using (var h = new ServerHarness())
            {
                h.Graph.Presentation.Properties["FullName"] = string.Empty;
                h.Graph.Presentation.Properties["Name"] = "Presentation1";
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Graph.Presentation.Properties["FullName"] = @"C:\Lessons\FirstSave.pptx";
                h.Graph.Presentation.Properties["Name"] = "FirstSave.pptx";
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                var descriptor = Json(h.Server.GetPresentationDescriptor());
                Check((string)descriptor["fullName"] == @"C:\Lessons\FirstSave.pptx" &&
                    (string)descriptor["presentationName"] == "FirstSave.pptx" && DescriptorStatus(h) == "StableSlideIds",
                    "first save replaces process-local identity with the newly saved path");
            }
            foreach (string stage in new[] { "presentation.FullName", "presentation.Name" })
            {
                using (var h = new ServerHarness())
                {
                    h.Call("RefreshPresentationDescriptorIfNeeded", true);
                    h.Graph.Presentation.Properties["FullName"] = @"C:\Lessons\BusySave.pptx";
                    h.Graph.Presentation.Properties["Name"] = "BusySave.pptx";
                    h.Accessor.FailureStage = stage;
                    h.Accessor.Failure = new COMException("busy", unchecked((int)0x8001010A));
                    h.Accessor.Operations.Clear();
                    h.Call("RefreshPresentationDescriptorIfNeeded", true);
                    var descriptor = Json(h.Server.GetPresentationDescriptor());
                    Check(DescriptorStatus(h) == "TransientBusy" && (string)descriptor["fullName"] == string.Empty &&
                        (string)Json(h.Server.GetSlideShowState(0))["pageStatus"] == "Unknown" && ItemReadCount(h) == 0,
                        stage + " busy publishes no borrowed cached file identity and performs no full scan");
                    h.Accessor.FailureStage = null;
                    h.Call("RefreshPresentationDescriptorIfNeeded", true);
                    descriptor = Json(h.Server.GetPresentationDescriptor());
                    Check((string)descriptor["fullName"] == @"C:\Lessons\BusySave.pptx" && DescriptorStatus(h) == "StableSlideIds",
                        stage + " recovery publishes the fresh saved identity");
                }
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Graph.Presentation.Properties.Remove("FullName");
                h.Graph.Presentation.Properties.Remove("Name");
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                var descriptor = Json(h.Server.GetPresentationDescriptor());
                Check(DescriptorStatus(h) == "Unavailable" && (string)descriptor["fullName"] == string.Empty &&
                    (string)descriptor["presentationName"] == string.Empty && h.Get("stablePresentationTopology") == null &&
                    Convert.ToInt32(descriptor["currentPage"]) == 2,
                    "missing identity clears cached names and ink trust while retaining fresh basic pages");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Accessor.FailureStage = "presentation.FullName";
                h.Accessor.Failure = new InvalidOperationException("identity read failed");
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check((string)Json(h.Server.GetPresentationDescriptor())["fullName"] == string.Empty,
                    "ordinary identity failure cannot leave the old saved path in a trusted descriptor");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                var unexpected = new FakeNode("unexpectedIdentity");
                h.Graph.Presentation.Properties["FullName"] = unexpected;
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(h.Outstanding("unexpectedIdentity") == 0 && h.Accessor.ReleaseCount("unexpectedIdentity") > 0 &&
                    (string)Json(h.Server.GetPresentationDescriptor())["fullName"] == string.Empty,
                    "unexpected COM identity scalar is released per acquisition and never revives the cached path");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                long publishedRevision = Convert.ToInt64(Json(h.Server.GetSlideShowState(0))["stateRevision"]);
                h.Graph.Presentation.Properties["FullName"] = @"C:\Lessons\LateSave.pptx";
                h.Graph.Presentation.Properties["Name"] = "LateSave.pptx";
                bool changed = false;
                h.Accessor.BeforeAccess = delegate(string stage)
                {
                    if (stage != "presentation.Name" || changed) return;
                    changed = true;
                    h.State.PageChanged();
                };
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Accessor.BeforeAccess = null;
                Check(Convert.ToInt64(Json(h.Server.GetSlideShowState(0))["stateRevision"]) == publishedRevision &&
                    h.Get("stablePresentationTopology") == null,
                    "identity read invalidated by a late generation cannot publish or replenish the cache");
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check((string)Json(h.Server.GetPresentationDescriptor())["fullName"] == @"C:\Lessons\LateSave.pptx",
                    "fresh generation recovers the changed identity after rejecting late work");
            }
        }

        private static void TestTopologyInvalidationMatrix()
        {
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                FakeNode added = new FakeNode("item4");
                added.Properties["SlideIndex"] = 4;
                added.Properties["SlideID"] = 404;
                Slides(h).Items.Add(added);
                Slides(h).Properties["Count"] = 4;
                h.State.PageChanged();
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 4 && DescriptorStatus(h) == "StableSlideIds",
                    "changed page count requires and replaces the complete topology");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                CurrentSlide(h).Properties["SlideID"] = 999;
                h.State.PageChanged();
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 3 && DescriptorStatus(h) == "PageIndexFallback" &&
                    h.Get("stablePresentationTopology") == null,
                    "current SlideID mismatch fully validates then safely degrades without a stable target");
                CurrentSlide(h).Properties["SlideID"] = 202;
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 3 && DescriptorStatus(h) == "StableSlideIds",
                    "a failed mismatch does not silently rehabilitate the suspicious cache");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                ((FakeNode)h.Graph.Window.Properties["View"]).Properties["State"] = 5;
                h.State.PageChanged();
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 3 && DescriptorStatus(h) == "StableSlideIds" &&
                    (string)Json(h.Server.GetSlideShowState(0))["pageStatus"] == "EndScreen",
                    "EndScreen always requires the complete unique topology");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Call("InvalidatePresentationDescriptor");
                long revision = (long)h.Get("presentationBindingRevision");
                h.State.Observe(revision, true, 100);
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 3 && DescriptorStatus(h) == "StableSlideIds",
                    "explicit invalidation discards the fast cache and performs full validation");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Accessor.FailureStage = "current.SlideID";
                h.Accessor.Failure = new MissingMemberException("SlideID");
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 0 && DescriptorStatus(h) == "PageIndexFallback" &&
                    h.Get("stablePresentationTopology") == null,
                    "missing current SlideID uses the established safe fallback instead of cached stable identity");
                h.Accessor.FailureStage = null;
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(DescriptorStatus(h) == "StableSlideIds", "full recovery follows the fallback");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Accessor.FailureStage = "current.SlideID";
                h.Accessor.Failure = new COMException("busy", unchecked((int)0x8001010A));
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 0 && DescriptorStatus(h) == "TransientBusy" &&
                    h.Get("stablePresentationTopology") != null,
                    "fast busy is retryable without a second full scan or damage to the last complete cache");
                h.Accessor.FailureStage = null;
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 0 && DescriptorStatus(h) == "StableSlideIds",
                    "busy recovery can reuse the intact complete cache");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                CurrentSlide(h).Properties["SlideID"] = 999;
                h.Accessor.FailureStage = "slides.Item:2";
                h.Accessor.Failure = new COMException("busy", unchecked((int)0x8001010A));
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(DescriptorStatus(h) == "TransientBusy" &&
                    h.Get("stablePresentationTopology") == null,
                    "full busy never publishes partial topology or preserves a mismatched cache");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                CurrentSlide(h).Properties["SlideID"] = 999;
                h.Accessor.FailureStage = "slides.Item:2";
                h.Accessor.Failure = new InvalidOperationException("broken topology");
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(DescriptorStatus(h) == "PageIndexFallback" &&
                    h.Get("stablePresentationTopology") == null &&
                    h.Outstanding("item2") == 0 && h.Outstanding("slides") == 0,
                    "ordinary full failure safely degrades, clears suspicious cache and releases partial acquisitions");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Call("InvalidatePresentationDescriptor");
                long revision = (long)h.Get("presentationBindingRevision");
                h.State.Observe(revision, true, 100);
                h.Accessor.AfterAcquire = delegate(string name)
                {
                    if (name == "item2") h.State.PageChanged();
                };
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(h.Get("stablePresentationTopology") == null &&
                    DescriptorStatus(h) == "Unavailable",
                    "late-generation full success cannot publish or replenish a stale topology");
                h.Accessor.AfterAcquire = null;
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(DescriptorStatus(h) == "StableSlideIds" &&
                    h.Get("stablePresentationTopology") != null,
                    "only a current-generation full success replenishes the cache");
            }
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                long publishedRevision = Convert.ToInt64(Json(h.Server.GetSlideShowState(0))["stateRevision"]);
                h.Accessor.AfterAcquire = delegate(string name)
                {
                    if (name == "slides") h.State.PageChanged();
                };
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(ItemReadCount(h) == 0 &&
                    Convert.ToInt64(Json(h.Server.GetSlideShowState(0))["stateRevision"]) == publishedRevision &&
                    h.Get("stablePresentationTopology") != null,
                    "late-generation fast success neither publishes nor discards the still valid complete cache");
                h.Accessor.AfterAcquire = null;
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(DescriptorStatus(h) == "StableSlideIds",
                    "a fresh fast generation can publish after the late candidate is rejected");
            }
        }

        private static void TestDelayedReorderDiscovery()
        {
            using (var h = new ServerHarness())
            {
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                FakeNode first = Slides(h).Items[0];
                FakeNode third = Slides(h).Items[2];
                first.Properties["SlideID"] = 303;
                third.Properties["SlideID"] = 101;
                h.State.PageChanged();
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                var descriptor = Json(h.Server.GetPresentationDescriptor());
                var ids = (System.Collections.ArrayList)descriptor["slideIds"];
                Check(ItemReadCount(h) == 0 && DescriptorStatus(h) == "StableSlideIds" &&
                    Convert.ToInt32(ids[0]) == 101,
                    "non-current background reorder uses the approved delayed-discovery fast path");

                FakeNode current = CurrentSlide(h);
                current.Properties["SlideIndex"] = 3;
                current.Properties["SlideID"] = 101;
                h.State.PageChanged();
                h.Accessor.Operations.Clear();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                descriptor = Json(h.Server.GetPresentationDescriptor());
                ids = (System.Collections.ArrayList)descriptor["slideIds"];
                Check(ItemReadCount(h) == 3 && DescriptorStatus(h) == "StableSlideIds" &&
                    Convert.ToInt32(ids[0]) == 303 && Convert.ToInt32(ids[2]) == 101,
                    "visiting a reordered index requires full validation before publishing its stable identity");
            }
        }

        private static void TestOwnerObservesBeforeQueuedCommand()
        {
            using (var h = new ServerHarness())
            {
                var mailbox = (SlideShowOwnerMailbox)h.Get("slideShowOwner");
                var queue = (Queue<SlideShowExitRequest>)typeof(SlideShowOwnerMailbox)
                    .GetField("exits", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(mailbox);
                queue.Enqueue(new SlideShowExitRequest(h.State.Capture(), SlideShowOwnerOperation.Next, true));
                h.Set("showObservationPending", 1);
                h.Accessor.Operations.Clear();
                Thread service = new Thread(delegate() { h.Server.PptComService(); });
                try
                {
                    service.Start();
                    DateTime deadline = DateTime.UtcNow.AddSeconds(2);
                    while (!h.Accessor.Operations.Contains("view.Next") && DateTime.UtcNow < deadline)
                        Thread.Sleep(5);
                    int observed = h.Accessor.Operations.IndexOf("view.Slide");
                    int next = h.Accessor.Operations.IndexOf("view.Next");
                    Check(observed >= 0 && next > observed,
                        "real service refreshes session/page evidence before executing an already queued command");
                }
                finally
                {
                    h.Stop();
                    service.Join(3000);
                }
            }
        }

        private static void TestMailboxBoundedDrain()
        {
            var mailbox = new SlideShowOwnerMailbox();
            var gate = typeof(SlideShowOwnerMailbox).GetField("gate", BindingFlags.Instance | BindingFlags.NonPublic)
                .GetValue(mailbox);
            var queue = (Queue<SlideShowExitRequest>)typeof(SlideShowOwnerMailbox)
                .GetField("exits", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(mailbox);
            Func<int> pending = delegate { lock (gate) return queue.Count; };
            mailbox.Start();
            Thread second = null;
            Thread first = new Thread(delegate() { mailbox.RequestExit(1, 2000); });
            first.Start();
            DateTime deadline = DateTime.UtcNow.AddSeconds(2);
            while (pending() == 0 && DateTime.UtcNow < deadline) Thread.Sleep(5);
            Check(pending() == 1, "bounded-drain fixture queues its first request");
            mailbox.ProcessExits(delegate(SlideShowExitRequest request)
            {
                second = new Thread(delegate() { mailbox.RequestExit(1, 2000); });
                second.Start();
                deadline = DateTime.UtcNow.AddSeconds(2);
                while (pending() == 0 && DateTime.UtcNow < deadline) Thread.Sleep(5);
                Check(pending() == 1, "a continuous producer enqueues during the active drain");
                return 1;
            });
            Check(first.Join(2000) && pending() == 1 && second != null && !second.Join(0),
                "a request arriving during execution is deferred so page maintenance cannot be starved");
            mailbox.ProcessExits(delegate { return 1; });
            Check(second != null && second.Join(2000) && pending() == 0,
                "the next bounded drain completes the deferred request");
            mailbox.Stop();
        }

        private static void TestOwnerCommandPriority()
        {
            using (var h = new ServerHarness())
            using (var entered = new ManualResetEvent(false))
            using (var release = new ManualResetEvent(false))
            {
                bool blocked = false;
                h.Accessor.BeforeAccess = delegate(string stage)
                {
                    if (stage != "view.State" || blocked) return;
                    blocked = true;
                    entered.Set();
                    release.WaitOne(2000);
                };
                int result = -2;
                Thread service = new Thread(delegate() { h.Server.PptComService(); });
                Thread client = null;
                try
                {
                    service.Start();
                    Check(entered.WaitOne(1000), "owner priority fixture blocks observation before maintenance");
                    ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).MaintenanceCompleted();
                    client = new Thread(delegate() { h.Server.NextSlideShow(true); result = 1; });
                    client.Start();
                    Thread.Sleep(50);
                    release.Set();
                    Check(client.Join(3000) && result == 1, "queued Next completes on the real owner");
                    DateTime deadline = DateTime.UtcNow.AddSeconds(2);
                    while (!h.Accessor.Operations.Contains("slides.Item:1") &&
                        DateTime.UtcNow < deadline) Thread.Sleep(5);
                    int next = h.Accessor.Operations.IndexOf("view.Next");
                    int full = h.Accessor.Operations.IndexOf("slides.Item:1");
                    Check(next >= 0 && full >= 0 && next < full,
                        "real service drains bounded Next before nonessential full descriptor maintenance");
                    Check(h.Accessor.IsBalanced() || h.Outstanding("application") == 1,
                        "command-priority run preserves acquisition/release accounting");
                }
                finally
                {
                    release.Set();
                    h.Accessor.BeforeAccess = null;
                    h.Stop();
                    if (client != null) client.Join(2000);
                    service.Join(3000);
                }
            }
        }

        private static Dictionary<string, string>[] PerfRecords(string text)
        {
            var records = new List<Dictionary<string, string>>();
            foreach (string line in text.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
            {
                if (!line.StartsWith("[PptPerf] ", StringComparison.Ordinal)) continue;
                var fields = new Dictionary<string, string>();
                foreach (string token in line.Substring(10).Split(' '))
                {
                    int equal = token.IndexOf('=');
                    if (equal > 0) fields[token.Substring(0, equal)] = token.Substring(equal + 1);
                }
                records.Add(fields);
            }
            return records.ToArray();
        }

        private static bool HasPerfStage(Dictionary<string, string>[] records, string stage, string kind)
        {
            foreach (var record in records)
                if (record["stage"] == stage && record["event"] == kind) return true;
            return false;
        }

        private static void CheckPerfPairs(Dictionary<string, string>[] records)
        {
            var open = new Dictionary<string, Dictionary<string, string>>();
            foreach (var record in records)
            {
                Check(record["v"] == "1" && record["source"] == "managed" &&
                    long.Parse(record["pid"]) > 0 && long.Parse(record["tid"]) > 0 &&
                    long.Parse(record["qpc"]) > 0 && long.Parse(record["frequency"]) > 0,
                    "perf records contain real numeric process/thread/QPC identity");
                if (record["event"] == "instant") continue;
                string span = record["span"];
                if (record["event"] == "begin") open.Add(span, record);
                else
                {
                    Dictionary<string, string> begin;
                    bool paired = open.TryGetValue(span, out begin);
                    Check(paired && begin["stage"] == record["stage"] && begin["request"] == record["request"] &&
                        long.Parse(record["qpc"]) >= long.Parse(begin["qpc"]),
                        "perf begin/end keep identity and monotonic timestamps including failures");
                    open.Remove(span);
                }
            }
            Check(open.Count == 0, "all synchronous perf scopes end without leaking thread context");
        }

        private static string PerfLedger(Dictionary<string, int> ledger)
        {
            var keys = new List<string>(ledger.Keys);
            keys.Sort(StringComparer.Ordinal);
            var values = new List<string>();
            foreach (string key in keys) values.Add(key + "=" + ledger[key]);
            return string.Join("|", values.ToArray());
        }

        private static string ObservePerfSnapshot(string scenario, bool enabled, out string trace)
        {
            var output = new System.IO.StringWriter();
            string snapshot;
            using (PptPerfTrace.UseTestOutput(enabled, output))
            using (var h = new ServerHarness())
            {
                var view = (FakeNode)h.Graph.Window.Properties["View"];
                if (scenario == "missing-count") h.Graph.Application.Properties.Remove("SlideShowWindows");
                if (scenario == "count-zero") h.Windows.Properties["Count"] = 0;
                if (scenario == "end-screen") view.Properties["State"] = 5;
                if (scenario == "missing-state") view.Properties.Remove("State");
                if (scenario == "missing-window") h.Graph.Presentation.Properties.Remove("SlideShowWindow");
                if (scenario == "active-failure" || scenario == "null-active")
                {
                    var presentations = new FakeNode("presentations");
                    presentations.Properties["Count"] = 0;
                    h.Graph.Application.Properties["Presentations"] = presentations;
                    if (scenario == "null-active") h.Graph.Application.Properties["ActivePresentation"] = null;
                    else
                    {
                        h.Accessor.FailureStage = "application.ActivePresentation";
                        h.Accessor.Failure = new COMException("no active presentation", unchecked((int)0x80020009));
                    }
                }
                if (scenario == "disconnected" || scenario == "busy-state" || scenario == "busy-count" ||
                    scenario == "busy-hwnd" || scenario == "failed-index")
                {
                    h.Accessor.FailureStage = scenario == "disconnected" ? "application.ActivePresentation" :
                        scenario == "busy-state" ? "view.State" : scenario == "busy-count" ? "windows.Count" :
                        scenario == "busy-hwnd" ? "window.HWND" : "current.SlideIndex";
                    h.Accessor.Failure = new COMException("injected observation failure", unchecked((int)(
                        scenario == "disconnected" ? 0x80010108 : scenario == "failed-index" ? 0x80004005 : 0x8001010A)));
                }
                if (scenario == "replaced-window")
                {
                    var replacement = new FakeNode("replacement");
                    foreach (var pair in h.Graph.Window.Properties) replacement.Properties[pair.Key] = pair.Value;
                    h.Graph.Presentation.Properties["SlideShowWindow"] = replacement;
                }
                if (scenario == "other-presentation")
                    h.Graph.Application.Properties["ActivePresentation"] = new FakeNode("other");
                if (scenario == "late-generation")
                    h.Accessor.AfterAcquire = delegate(string name) { if (name == "view") h.State.PageChanged(); };

                h.Accessor.Operations.Clear(); h.Accessor.ReleaseOrder.Clear();
                bool observed = h.Observe();
                var stamp = h.State.Capture();
                string operations = string.Join("|", h.Accessor.Operations.ToArray());
                string releases = string.Join("|", h.Accessor.ReleaseOrder.ToArray());
                if (scenario == "normal")
                {
                    Check(operations == "application.ActivePresentation|application.SlideShowWindows|windows.Count|" +
                        "presentation.SlideShowWindow|presentation.Slides|slides.Count|window.View|view.State|" +
                        "view.Slide|current.SlideIndex|window.HWND",
                        "substage timing preserves the original normal COM read order and count");
                    Check(releases == "windows|slides|current|view|window|presentation" &&
                        h.Outstanding("application") == 1 && h.Outstanding("presentation") == 1 && h.Outstanding("window") == 1,
                        "substage timing releases original acquisitions child-first without releasing borrowed roots");
                }
                snapshot = observed + ":" + h.Current + ":" + h.Total + ":" + h.Get("polling") + ":" +
                    stamp.Lifecycle + ":" + stamp.Generation + ":" + stamp.Session + ":" + stamp.Binding + ":" + stamp.Window +
                    "\n" + operations + "\n" + releases + "\n" + PerfLedger(h.Accessor.Acquired) + "\n" +
                    PerfLedger(h.Accessor.Released) + "\n" + h.Accessor.ReleaseErrors;
            }
            trace = output.ToString();
            return snapshot;
        }

        private static void TestObservePerfSubstages()
        {
            foreach (string scenario in new[] { "normal", "missing-count", "count-zero", "end-screen", "missing-state",
                "missing-window", "active-failure", "null-active", "disconnected", "busy-state", "busy-count",
                "busy-hwnd", "failed-index", "replaced-window", "other-presentation", "late-generation" })
            {
                string silent, trace;
                string original = ObservePerfSnapshot(scenario, false, out silent);
                string instrumented = ObservePerfSnapshot(scenario, true, out trace);
                Check(silent.Length == 0 && original == instrumented,
                    scenario + " substage enable/disable preserves calls, release ledger, page values and lifecycle/generation");
                var records = PerfRecords(trace);
                CheckPerfPairs(records);
                Check(HasPerfStage(records, "observe-temporaries-release", "begin"),
                    scenario + " final temporary releases are timed even on early return or failure");
                if (scenario == "normal")
                {
                    foreach (string stage in new[] { "observe-active-presentation", "observe-presentation-identity",
                        "observe-show-count", "observe-window-get", "observe-total", "observe-view-get",
                        "observe-view-state", "observe-current", "observe-view-release", "observe-hwnd",
                        "observe-window-identity", "observe-session-commit", "read-current-slide-get",
                        "read-current-slide-index", "read-current-slide-release", "read-slides-get",
                        "read-slides-count", "read-slides-release", "read-show-windows-get",
                        "read-show-windows-count", "read-show-windows-release" })
                        Check(HasPerfStage(records, stage, "begin"), "normal observation covers " + stage);
                    var begin = new Dictionary<string, Dictionary<string, string>>();
                    foreach (var record in records) if (record["event"] == "begin") begin[record["span"]] = record;
                    foreach (var record in records)
                    {
                        if (record["event"] != "begin" ||
                            !(record["stage"].StartsWith("observe-", StringComparison.Ordinal) ||
                              record["stage"].StartsWith("read-", StringComparison.Ordinal))) continue;
                        Dictionary<string, string> parent;
                        Check(begin.TryGetValue(record["parent"], out parent) &&
                            (record["stage"].StartsWith("read-slides-", StringComparison.Ordinal) ? parent["stage"] == "observe-total" :
                             record["stage"].StartsWith("read-current-slide-", StringComparison.Ordinal) ? parent["stage"] == "observe-current" :
                             record["stage"].StartsWith("read-show-windows-", StringComparison.Ordinal) ? parent["stage"] == "observe-show-count" :
                             parent["stage"] == "observe"), "observation children retain their actual nested stage parent");
                    }
                }
                if (scenario == "missing-count")
                    Check(HasPerfStage(records, "observe-window-presentation", "begin") &&
                        HasPerfStage(records, "observe-window-presentation-identity", "begin"),
                        "missing Count retains timed fresh Presentation fallback without skipping its identity check");
                if (scenario == "active-failure" || scenario == "null-active")
                    foreach (string stage in new[] { "observe-presentations-count", "read-presentations-get",
                        "read-presentations-count", "read-presentations-release" })
                        Check(HasPerfStage(records, stage, "begin"), "last-document fallback covers " + stage);
                if (scenario == "count-zero" || scenario == "replaced-window")
                    Check(HasPerfStage(records, "observe-retired-window-release", "begin"),
                        "retiring an owned show window remains separate from final borrowed temporary cleanup");
                if (scenario == "end-screen")
                    Check(!HasPerfStage(records, "observe-current", "begin"), "confirmed end screen still avoids View.Slide");
                Check(!trace.Contains("Lessons") && !trace.Contains("Deck.ppt"), "observation substage output has no document identity");
            }
        }

        private static void TestNavigationPerfDiagnostics()
        {
            var silent = new System.IO.StringWriter();
            using (PptPerfTrace.UseTestOutput(false, silent))
            using (var h = new ServerHarness())
            {
                h.Observe(); h.Command(SlideShowOwnerOperation.Next, true);
                Check(silent.ToString().Length == 0, "disabled perf output is silent on real observe/command paths");
            }
            var output = new System.IO.StringWriter();
            using (PptPerfTrace.UseTestOutput(true, output))
            using (var h = new ServerHarness())
            {
                h.Observe();
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                h.Call("RefreshSlideShowAnnotationTool");
                var mailbox = (SlideShowOwnerMailbox)h.Get("slideShowOwner");
                var gate = typeof(SlideShowOwnerMailbox).GetField("gate", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(mailbox);
                var queue = (Queue<SlideShowExitRequest>)typeof(SlideShowOwnerMailbox)
                    .GetField("exits", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(mailbox);
                mailbox.Start();
                // 实际2秒等待返回后才放开假Office调用，验证晚完成并非取消执行。
                using (var release = new ManualResetEvent(false))
                {
                    h.Accessor.BeforeAccess = delegate(string stage)
                    {
                        if (stage == "view.Previous") Check(release.WaitOne(3000), "waiter timeout releases the simulated Office call");
                    };
                    var client = new Thread(delegate() { h.Server.PreviousSlideShow(); release.Set(); });
                    client.Start();
                    DateTime deadline = DateTime.UtcNow.AddSeconds(2);
                    bool queued = false;
                    while (!queued && DateTime.UtcNow < deadline) { lock (gate) queued = queue.Count != 0; if (!queued) Thread.Sleep(1); }
                    Check(queued, "perf test reaches the production owner mailbox");
                    h.Call("ProcessSlideShowExitRequests");
                    Check(client.Join(3000), "timed-out native waiter returns before late owner completion");
                    h.Accessor.BeforeAccess = null;
                }
                var stoppedClient = new Thread(delegate() { h.Server.NextSlideShow(true); });
                stoppedClient.Start();
                DateTime stopDeadline = DateTime.UtcNow.AddSeconds(2);
                bool pendingStop = false;
                while (!pendingStop && DateTime.UtcNow < stopDeadline) { lock (gate) pendingStop = queue.Count != 0; if (!pendingStop) Thread.Sleep(1); }
                Check(pendingStop, "stop regression reaches an actual queued request");
                mailbox.Stop();
                Check(stoppedClient.Join(3000), "stop drains the request without starting Office");
                mailbox.Start();
                var cancelledRequest = new SlideShowExitRequest(h.State.Capture(), SlideShowOwnerOperation.Previous, false);
                cancelledRequest.DiagnosticId = long.MaxValue;
                var cancelledClient = new Thread(delegate() { Check(mailbox.Request(cancelledRequest, 1) == -1, "queued short timeout remains cancelled"); });
                cancelledClient.Start();
                Check(cancelledClient.Join(3000), "queued request expires without an owner execution");
                h.Call("ProcessSlideShowExitRequests");
                h.Server.NextSlideShow(true); // owner旁路仍有request/owner，不伪造排队事件。
                h.Accessor.FailureStage = "view.Next";
                h.Accessor.Failure = new COMException("busy", unchecked((int)0x8001010A));
                Check(h.Command(SlideShowOwnerOperation.Next, true) == -1, "perf leaves actual command HRESULT behavior unchanged");
                h.Accessor.FailureStage = null;
                mailbox.Stop();
            }
            var records = PerfRecords(output.ToString());
            CheckPerfPairs(records);
            foreach (string stage in new[] { "observe", "descriptor-fast", "descriptor-full", "caption", "annotation",
                "managed-request", "mailbox-wait", "owner", "command-precheck", "view-get", "invoke" })
                Check(HasPerfStage(records, stage, "begin"), "real perf coverage for " + stage);
            Check(HasPerfStage(records, "mailbox-enqueue", "instant") && HasPerfStage(records, "mailbox-dequeue", "instant") &&
                HasPerfStage(records, "mailbox-complete", "instant") && HasPerfStage(records, "mailbox-stop-drain", "instant") &&
                HasPerfStage(records, "mailbox-skipped", "instant"), "actual queue transitions include stop and queued-timeout terminal states");
            Dictionary<string, string> timedOut = null, lateOwner = null;
            foreach (var record in records)
            {
                if (record["stage"] == "mailbox-wait" && record["event"] == "end" && record["result"] == "-1" &&
                    record["executing"] == "1" && record["completed"] == "0") timedOut = record;
                if (timedOut != null && record["stage"] == "owner" && record["event"] == "end" && record["request"] == timedOut["request"]) lateOwner = record;
            }
            Check(timedOut != null && lateOwner != null && timedOut["executing"] == "1" && timedOut["completed"] == "0" &&
                lateOwner["result"] == "1" && long.Parse(lateOwner["qpc"]) > long.Parse(timedOut["qpc"]),
                "trace distinguishes executing timeout from actual late successful completion");
            Check(!output.ToString().Contains("Lessons") && !output.ToString().Contains("Deck.ppt"),
                "perf output never includes document paths or titles");
            using (PptPerfTrace.UseTestOutput(true, new ThrowingCleanupWriter()))
            using (var h = new ServerHarness())
                Check(h.Observe() && h.Command(SlideShowOwnerOperation.Next, true) == 1,
                    "throwing perf sink cannot change real command execution or cleanup");
        }


        private static string RunShutdownCleanupTrace(bool enabled, bool final, bool throughService,
            int connectionMode, out Dictionary<string, string>[] records)
        {
            var output = new System.IO.StringWriter();
            using (PptPerfTrace.UseTestOutput(enabled, output))
            using (var h = new ServerHarness())
            {
                int disconnectAttempts = 0, pointReleases = 0;
                bool finishPending = false;
                if (connectionMode != 0)
                {
                    var point = new FakeNode("shutdownPoint");
                    h.Graph.Application.Properties["ConnectionPoint"] = point;
                    object acquisition = h.Accessor.GetProperty(h.Graph.Application, "ConnectionPoint");
                    h.Set("eventConnection", new ComEventConnection(71,
                        cookie =>
                        {
                            ++disconnectAttempts;
                            Check(h.Get("eventConnection") == null,
                                "cleanup detaches its connection slot before the existing disconnect call");
                            return finishPending || connectionMode == 1 ||
                                (connectionMode == 2 && disconnectAttempts >= 3)
                                ? 0 : unchecked((int)0x8001010A);
                        },
                        () => { ++pointReleases; h.Accessor.Release(acquisition); }, delegate { }));
                }
                h.Set("rotScanDeferredLastPass", true);
                h.Set("annotationToolSession", 9L);
                h.Set("annotationToolBinding", 8L);
                h.Set("annotationToolWindow", 7L);
                h.Set("annotationToolCache", 3);
                h.Accessor.Operations.Clear();
                try
                {
                    int result = 0;
                    if (throughService)
                    {
                        // 自有退出槽在进入监测循环前置位；真实 owner finally 无 Office/ROT 扫描。
                        h.Stop();
                        result = h.Server.PptComService();
                    }
                    else h.Call("FullCleanup", final);
                    int pending = ComEventRetirementRegistry.PendingCount;
                    Check(h.Accessor.Operations.Count == 0,
                        "cleanup telemetry never reads Office properties to produce a marker");
                    Check(h.Get("pptApplication") == null && h.Get("pptActivePresentation") == null &&
                        h.Get("pptSlideShowWindow") == null && h.Current == -1 && h.Total == -1 &&
                        !(bool)h.Get("rotScanDeferredLastPass") && !(bool)h.Get("bindingEvents") &&
                        (bool)h.Get("forcePolling") && (long)h.Get("annotationToolSession") == 0 &&
                        (long)h.Get("annotationToolBinding") == 0 && (long)h.Get("annotationToolWindow") == 0 &&
                        (int)h.Get("annotationToolCache") == 0,
                        "shutdown markers preserve root, raw-page, annotation and polling reset state");
                    string roots = string.Join("|", h.Accessor.ReleaseOrder.ToArray());
                    string expected = connectionMode == 1 ? "shutdownPoint|window|presentation|application"
                        : "window|presentation|application" +
                            (connectionMode == 2 ? "|shutdownPoint" : string.Empty);
                    Check(roots == expected, "cleanup still releases each acquisition in the original exact order");
                    Check(result == (connectionMode == 3 ? -1 : 0) &&
                        pending == (connectionMode == 3 ? 1 : 0) &&
                        pointReleases == (connectionMode == 0 || connectionMode == 3 ? 0 : 1),
                        "cleanup retains failed CP leases and original service result without GC");
                    records = PerfRecords(output.ToString());
                    if (enabled)
                    {
                        CheckPerfPairs(records);
                        foreach (string stage in new[] { final ? "cleanup-final" : "cleanup-rebind",
                            "cleanup-invalidate", "cleanup-annotation-reset", "cleanup-unbind",
                            "cleanup-message-before", "cleanup-release-window", "cleanup-release-presentation",
                            "cleanup-release-application", "cleanup-reset-roots", "cleanup-reset-pages",
                            "cleanup-message-after" })
                            Check(HasPerfStage(records, stage, "begin"), "cleanup stage is paired: " + stage);
                        Check(!HasPerfStage(records, final ? "cleanup-rebind" : "cleanup-final", "begin"),
                            "rebind cleanup is never mistaken for final shutdown");
                        Check(HasPerfStage(records, "shutdown-managed-finally", "begin") == throughService,
                            "owner finalization marker only surrounds the service finally");
                        if (throughService)
                            foreach (string stage in new[] { "shutdown-mailbox-stop", "shutdown-retired-events" })
                                Check(HasPerfStage(records, stage, "end"), "service finally completes stage " + stage);
                        if (connectionMode != 0)
                            Check(HasPerfStage(records, "cleanup-event-deactivate", "end") &&
                                HasPerfStage(records, "cleanup-event-disconnect", "end"),
                                "existing connection deactivate and disconnect have separate boundaries");
                        if (connectionMode >= 2)
                            Check(HasPerfStage(records, "cleanup-event-retire", "end") &&
                                HasPerfStage(records, "shutdown-retired-event-retry", "end") &&
                                HasPerfStage(records, "shutdown-retired-event-wait", "end"),
                                "failed disconnect keeps original bounded retirement retries and waits");
                        Check(!output.ToString().Contains("Lessons") && !output.ToString().Contains("Deck.ppt"),
                            "shutdown output contains no document path or title");
                    }
                    else Check(output.ToString().Length == 0, "disabled cleanup diagnostics are silent");
                    return string.Join("|", h.Accessor.Operations.ToArray()) + ";" + roots + ";" +
                        PerfLedger(h.Accessor.Acquired) + ";" + PerfLedger(h.Accessor.Released) + ";" +
                        h.State.GetSince(-1) + ";" + result + ";" + pending + ";" + disconnectAttempts;
                }
                finally
                {
                    // 永久busy场景只在断言后显式放行其自有CP，不把保留lease留给下一测试。
                    finishPending = true;
                    h.Call("RetryRetiredEventConnections");
                    Check(ComEventRetirementRegistry.PendingCount == 0,
                        "cleanup fixture explicitly consumes its retained CP before disposing");
                }
            }
        }

        private static void TestShutdownCleanupPerfDiagnostics()
        {
            foreach (int scenario in new[] { 0, 1, 2, 3, 4, 5 })
            {
                bool throughService = scenario >= 2;
                bool final = scenario != 0;
                int connectionMode = scenario < 3 ? 0 : scenario - 2;
                Dictionary<string, string>[] disabled, enabled;
                string baseline = RunShutdownCleanupTrace(false, final, throughService, connectionMode, out disabled);
                string measured = RunShutdownCleanupTrace(true, final, throughService, connectionMode, out enabled);
                Check(baseline == measured && disabled.Length == 0 && enabled.Length != 0,
                    "cleanup enabled/disabled exact operation, receipt and state equivalence case " + scenario);
            }
            using (PptPerfTrace.UseTestOutput(true, new ThrowingCleanupWriter()))
            using (var h = new ServerHarness())
            {
                h.Stop();
                Check(h.Server.PptComService() == 0 && h.Accessor.IsBalanced(),
                    "closed perf writer cannot interrupt owner finally or deterministic root release");
            }
        }

        // 只替换系统 ROT 接口；真实 scanner、dynamic accessor 和所有 finally 仍是产品路径。
        private sealed class RotFixture : RotTable
        {
            public readonly ServerHarness Harness;
            public readonly List<FakeNode> Rows = new List<FakeNode>();
            public readonly List<string> Steps = new List<string>();
            public Action<string> OnStep;
            public string FailAt;
            public int AcquireHresult, ObjectCalls, BindContextsSeen, BindContextReleases, ReleaseErrors;
            private readonly Dictionary<object, int> received = new Dictionary<object, int>();
            private readonly Dictionary<object, int> released = new Dictionary<object, int>();
            public RotFixture(ServerHarness harness)
            {
                Harness = harness;
                harness.Graph.Presentation.Properties["Application"] = harness.Graph.Application;
                Rows.Add(harness.Graph.Presentation);
            }
            private void Step(string name)
            {
                Steps.Add(name);
                if (OnStep != null) OnStep(name);
                if (FailAt == name) throw new COMException("fixture failure", unchecked((int)0x80004005));
            }
            private T Receive<T>(T value)
            {
                object key = value; int count; received.TryGetValue(key, out count); received[key] = count + 1;
                return value;
            }
            public int Acquire(out RotTable table) { table = Receive<RotTable>(this); Step("acquire"); return AcquireHresult; }
            public void Release(object value)
            {
                if (value == null) return;
                if (value is FakeNode) { Harness.Accessor.Release(value); return; }
                if (Marshal.IsComObject(value))
                {
                    Marshal.ReleaseComObject(value); // CreateBindCtx仍是真实RCW，测试也逐笔释放。
                    ++BindContextReleases; // 只统计实际成功返回的Release，不把吞掉的异常算作通过。
                    return;
                }
                int count, used; received.TryGetValue(value, out count); released.TryGetValue(value, out used);
                if (used >= count) { ++ReleaseErrors; throw new InvalidOperationException("duplicate ROT receipt release"); }
                released[value] = used + 1;
            }
            public bool Balanced()
            {
                if (ReleaseErrors != 0 || BindContextReleases != BindContextsSeen) return false;
                foreach (var receipt in received)
                { int count; released.TryGetValue(receipt.Key, out count); if (count != receipt.Value) return false; }
                return true;
            }
            public void EnumRunning(out RotEnumerator result)
            { result = Receive<RotEnumerator>(new Enumeration(this)); Step("enum"); }
            public int GetObject(RotMoniker moniker, out object result)
            {
                int index = ((Moniker)moniker).Index;
                var holder = new FakeNode("rot-holder"); holder.Properties["value"] = Rows[index];
                result = Harness.Accessor.GetProperty(holder, "value"); ++ObjectCalls; Step("object:" + index); return 0;
            }
            private sealed class Enumeration : RotEnumerator
            {
                private readonly RotFixture fixture; private int index;
                public Enumeration(RotFixture fixture) { this.fixture = fixture; }
                public int Next(int count, RotMoniker[] values, IntPtr fetched)
                {
                    if (index >= fixture.Rows.Count) { fixture.Step("next:end"); return 1; }
                    int current = index++; values[0] = fixture.Receive<RotMoniker>(new Moniker(fixture, current));
                    fixture.Step("next:" + current); return 0;
                }
                public int Skip(int count) { index += count; return index < fixture.Rows.Count ? 0 : 1; }
                public void Reset() { index = 0; }
                public void Clone(out RotEnumerator copy) { throw new NotSupportedException(); }
            }
            private sealed class Moniker : RotMoniker
            {
                private readonly RotFixture fixture; public readonly int Index;
                public Moniker(RotFixture fixture, int index) { this.fixture = fixture; Index = index; }
                public void GetDisplayName(RotBind context, RotMoniker left, out string name)
                {
                    // 只计实际收到的非空context；未进入/失败的CreateBindCtx不臆造取得。
                    if (context != null) ++fixture.BindContextsSeen;
                    name = "fixture-" + Index + ".pptx"; fixture.Step("name:" + Index);
                }
                public void GetClassID(out Guid value) { value = Guid.Empty; }
                public int IsDirty() { return 1; }
                public void Load(RotStream stream) { throw new NotSupportedException(); }
                public void Save(RotStream stream, bool clear) { throw new NotSupportedException(); }
                public void GetSizeMax(out long size) { size = 0; }
                public void BindToObject(RotBind c, RotMoniker l, ref Guid id, out object value) { throw new NotSupportedException(); }
                public void BindToStorage(RotBind c, RotMoniker l, ref Guid id, out object value) { throw new NotSupportedException(); }
                public void Reduce(RotBind c, int how, ref RotMoniker left, out RotMoniker reduced) { throw new NotSupportedException(); }
                public void ComposeWith(RotMoniker right, bool generic, out RotMoniker result) { throw new NotSupportedException(); }
                public void Enum(bool forward, out RotEnumerator result) { throw new NotSupportedException(); }
                public int IsEqual(RotMoniker other) { return ReferenceEquals(this, other) ? 0 : 1; }
                public void Hash(out int value) { value = Index; }
                public int IsRunning(RotBind c, RotMoniker l, RotMoniker n) { return 0; }
                public void GetTimeOfLastChange(RotBind c, RotMoniker l, out RotFileTime time) { time = new RotFileTime(); }
                public void Inverse(out RotMoniker value) { throw new NotSupportedException(); }
                public void CommonPrefixWith(RotMoniker other, out RotMoniker result) { throw new NotSupportedException(); }
                public void RelativePathTo(RotMoniker other, out RotMoniker result) { throw new NotSupportedException(); }
                public void ParseDisplayName(RotBind c, RotMoniker l, string name, out int eaten, out RotMoniker result) { throw new NotSupportedException(); }
                public int IsSystemMoniker(out int type) { type = 0; return 1; }
            }
            public int Register(int flags, object value, RotMoniker moniker) { throw new NotSupportedException(); }
            public void Revoke(int cookie) { throw new NotSupportedException(); }
            public int IsRunning(RotMoniker moniker) { return 0; }
            public void NoteChangeTime(int cookie, ref RotFileTime time) { throw new NotSupportedException(); }
            public int GetTimeOfLastChange(RotMoniker moniker, out RotFileTime time) { time = new RotFileTime(); return 0; }
        }
        private sealed class TriggerRotWriter : System.IO.StringWriter
        {
            public Action<string> Written;
            public override void WriteLine(string value) { if (Written != null) Written(value); base.WriteLine(value); }
        }
        private static void WithRotFixture(Action<ServerHarness, RotFixture> test)
        {
            RotFixture fixture = null;
            using (var h = new ServerHarness(delegate(out RotTable table) { return fixture.Acquire(out table); },
                value => fixture.Release(value)))
            {
                fixture = new RotFixture(h);
                h.Graph.Window.Properties["Active"] = -1;
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                test(h, fixture);
                Check(fixture.Balanced(), "every ROT/enum/moniker receipt is released exactly once");
                Check(fixture.BindContextReleases == fixture.BindContextsSeen,
                    "every actual nonnull CreateBindCtx receipt has one successful Marshal release on normal/deferred/error paths");
            }
        }
        private static object ScanRot(ServerHarness h, Func<bool> pending, out bool deferred)
        {
            object[] args = { h.Graph.Application, pending, 0, 0, false };
            object result = h.Call("ScanActivePowerPoint", args);
            deferred = (bool)args[4];
            if (deferred) Check(result == null && (int)args[2] == 0 && (int)args[3] == 0,
                "deferred discovery never transfers a partial winner or its priorities");
            return result;
        }
        private static Queue<SlideShowExitRequest> RotQueue(ServerHarness h)
        {
            return (Queue<SlideShowExitRequest>)typeof(SlideShowOwnerMailbox)
                .GetField("exits", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(h.Get("slideShowOwner"));
        }
        private static void QueueRotNavigation(ServerHarness h, SlideShowOwnerOperation op)
        {
            var mailbox = (SlideShowOwnerMailbox)h.Get("slideShowOwner");
            var gate = typeof(SlideShowOwnerMailbox).GetField("gate", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(mailbox);
            lock (gate) RotQueue(h).Enqueue(new SlideShowExitRequest(h.State.Capture(), op, false));
        }
        private static void CheckRotRoots(ServerHarness h)
        {
            Check(h.Outstanding("application") == 1 && h.Outstanding("presentation") == 1 && h.Outstanding("window") == 1,
                "discovery temporaries are released without releasing any borrowed bound root");
        }
        private static void TestRotYieldOwnership()
        {
            int checkpoints = 0;
            WithRotFixture((h, f) =>
            {
                bool deferred;
                object winner = ScanRot(h, () => { ++checkpoints; return false; }, out deferred);
                Check(!deferred && ReferenceEquals(winner, h.Graph.Application), "complete scanner transfers its one winning acquisition");
                h.Accessor.Release(winner); CheckRotRoots(h);
            });
            for (int stop = 1; stop <= checkpoints; ++stop)
            {
                int selected = stop;
                WithRotFixture((h, f) =>
                {
                    int visited = 0; bool deferred;
                    ScanRot(h, () => ++visited == selected, out deferred);
                    Check(deferred, "every reached safe scanner checkpoint can defer"); CheckRotRoots(h);
                });
            }
            WithRotFixture((h, f) =>
            {
                f.Rows.Add(h.Graph.Presentation); bool pending = false, deferred;
                f.OnStep = stage => { if (stage == "next:1") pending = true; };
                ScanRot(h, () => pending, out deferred);
                Check(deferred && f.ObjectCalls == 1, "pending after partial winner unwinds even the newly returned next moniker");
                CheckRotRoots(h);
            });
            WithRotFixture((h, f) =>
            {
                f.Rows.Add(h.Graph.Presentation); bool pending = false, deferred;
                var saved = Console.Out;
                var writer = new TriggerRotWriter { Written = line => { if (line.Contains("Deduplication")) pending = true; } };
                try { Console.SetOut(writer); ScanRot(h, () => pending, out deferred); }
                finally { Console.SetOut(saved); }
                Check(deferred && f.ObjectCalls == 2, "same-identity duplicate and selected list receipt are each released once on defer");
                CheckRotRoots(h);
            });
            WithRotFixture((h, f) =>
            {
                h.Graph.Window.Properties["Active"] = false;
                var app = new FakeNode("other-app"); var pres = new FakeNode("other-pres"); var window = new FakeNode("other-window");
                pres.Properties["Application"] = app; app.Properties["ActivePresentation"] = pres;
                pres.Properties["SlideShowWindow"] = window; window.Properties["Active"] = -1;
                f.Rows.Add(pres); f.Rows.Add(pres);
                bool pending = false, deferred;
                f.OnStep = stage => { if (stage == "next:2") pending = true; };
                ScanRot(h, () => pending, out deferred);
                Check(deferred && f.ObjectCalls == 2 && h.Outstanding("other-app") == 0 &&
                    h.Outstanding("other-pres") == 0 && h.Outstanding("other-window") == 0,
                    "defer after a higher-priority replacement releases both selected aliases and every list acquisition");
                CheckRotRoots(h);
            });
            WithRotFixture((h, f) =>
            {
                bool pending = false, deferred;
                f.OnStep = stage => { if (stage == "next:end") pending = true; };
                object winner = ScanRot(h, () => pending, out deferred);
                Check(!deferred && winner != null, "a request arriving after enumeration completion does not discard complete discovery");
                h.Accessor.Release(winner); CheckRotRoots(h);
            });
            WithRotFixture((h, f) =>
            {
                f.AcquireHresult = unchecked((int)0x80004005); bool deferred;
                Check(ScanRot(h, () => false, out deferred) == null && !deferred,
                    "failed GetROT with nonnull receipt still unwinds its outer finally"); CheckRotRoots(h);
            });
            WithRotFixture((h, f) =>
            {
                f.FailAt = "name:0"; bool deferred;
                Check(ScanRot(h, () => false, out deferred) == null && !deferred &&
                    f.BindContextsSeen == 1 && f.BindContextReleases == 1,
                    "display-name failure still releases the actual bind context received before the error");
                CheckRotRoots(h);
            });
            WithRotFixture((h, f) =>
            {
                f.FailAt = "object:0"; bool deferred;
                Check(ScanRot(h, () => false, out deferred) == null && !deferred,
                    "object acquisition failure after receiving an object releases the receipt"); CheckRotRoots(h);
            });
            WithRotFixture((h, f) =>
            {
                var saved = Console.Out; bool deferred; object winner;
                try { Console.SetOut(new ThrowingCleanupWriter()); winner = ScanRot(h, () => false, out deferred); }
                finally { Console.SetOut(saved); }
                Check(winner != null && !deferred, "throwing scan diagnostics cannot orphan a selected winner");
                h.Accessor.Release(winner); CheckRotRoots(h);
            });
        }
        private static void TestRotYieldFairnessAndObservation()
        {
            WithRotFixture((h, f) =>
            {
                QueueRotNavigation(h, SlideShowOwnerOperation.Previous);
                h.Call("MaintainBinding");
                Check(f.ObjectCalls == 0 && (bool)h.Get("rotScanDeferredLastPass"), "healthy pending navigation defers the first discovery");
                h.Call("MaintainBinding");
                Check(f.ObjectCalls == 1 && !(bool)h.Get("rotScanDeferredLastPass"), "next due discovery is forced despite continued queued navigation");
                h.Call("MaintainBinding");
                Check(f.ObjectCalls == 1 && (bool)h.Get("rotScanDeferredLastPass"), "a completed full attempt restores exactly one deferral allowance");
                h.Call("FullCleanup", false);
                Check(!(bool)h.Get("rotScanDeferredLastPass"), "cleanup resets the discovery fairness budget");
                ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).Stop();
            });
            foreach (string status in new[] { "TransientBusy", "Unavailable" })
                WithRotFixture((h, f) =>
                {
                    var d = (PresentationDescriptorValue)h.Get("presentationDescriptorCache"); d.status = status;
                    QueueRotNavigation(h, SlideShowOwnerOperation.Previous); h.Call("MaintainBinding");
                    Check(f.ObjectCalls == 1 && !(bool)h.Get("rotScanDeferredLastPass"), "Active lifecycle with unusable descriptor never defers discovery");
                    ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).Stop();
                });
            WithRotFixture((h, f) =>
            {
                h.State.Observe(1, null, 0); QueueRotNavigation(h, SlideShowOwnerOperation.Previous); h.Call("MaintainBinding");
                Check(f.ObjectCalls == 1 && !(bool)h.Get("rotScanDeferredLastPass"), "Unknown lifecycle uses original full discovery");
                ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).Stop();
            });
            WithRotFixture((h, f) =>
            {
                h.Set("presentationDescriptorRetryUtc", DateTime.MinValue);
                QueueRotNavigation(h, SlideShowOwnerOperation.Previous); h.Call("MaintainBinding");
                Check(f.ObjectCalls == 1 && !(bool)h.Get("rotScanDeferredLastPass"), "retry recovery keeps full discovery even with a last valid descriptor");
                ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).Stop();
            });
            WithRotFixture((h, f) =>
            {
                var view = (FakeNode)h.Graph.Window.Properties["View"];
                ((FakeNode)view.Properties["Slide"]).Properties.Remove("SlideID");
                h.Call("RefreshPresentationDescriptorIfNeeded", true);
                Check(((PresentationDescriptorValue)h.Get("presentationDescriptorCache")).status == "PageIndexFallback", "fixture establishes original fallback capability");
                QueueRotNavigation(h, SlideShowOwnerOperation.Previous); h.Call("MaintainBinding");
                Check(f.ObjectCalls == 0 && (bool)h.Get("rotScanDeferredLastPass"), "known PageIndexFallback retains basic-control deferral eligibility");
                ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).Stop();
            });
            WithRotFixture((h, f) =>
            {
                h.Call("FullCleanup", false);
                QueueRotNavigation(h, SlideShowOwnerOperation.Previous); h.Call("MaintainBinding");
                Check(f.ObjectCalls == 1 && !(bool)h.Get("rotScanDeferredLastPass") && h.Get("pptApplication") != null,
                    "an unbound owner always completes discovery rather than deferring it");
                Check(h.Observe(), "new discovery still takes full current-show observation");
                h.Call("ProcessSlideShowExitRequests");
                Check(!h.Accessor.Invoked.Contains("view.Previous"), "old pre-binding request cannot execute after full rediscovery");
            });
            foreach (var operation in new[] { SlideShowOwnerOperation.ExitShow, SlideShowOwnerOperation.ExitAnnotation })
                WithRotFixture((h, f) =>
                {
                    QueueRotNavigation(h, operation); h.Call("MaintainBinding");
                    Check(f.ObjectCalls == 1, "exit or annotation alone does not broaden navigation-only ROT deferral");
                    ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).Stop();
                });
            WithRotFixture((h, f) =>
            {
                h.Graph.Window.Properties["HWND"] = 0L;
                h.Observe(); h.Call("RefreshPresentationDescriptorIfNeeded", true);
                QueueRotNavigation(h, SlideShowOwnerOperation.Previous); h.Call("MaintainBinding");
                Check(f.ObjectCalls == 0 && (bool)h.Get("rotScanDeferredLastPass"), "HWND zero does not block healthy object-directed basic control eligibility");
                ((SlideShowOwnerMailbox)h.Get("slideShowOwner")).Stop();
            });
            WithRotFixture((h, f) =>
            {
                var mailbox = (SlideShowOwnerMailbox)h.Get("slideShowOwner"); mailbox.Start();
                Thread client = null;
                f.OnStep = stage =>
                {
                    if (stage != "object:0" || client != null) return;
                    client = new Thread(delegate() { h.Server.PreviousSlideShow(); }); client.Start();
                    DateTime deadline = DateTime.UtcNow.AddSeconds(1);
                    while (!mailbox.HasPendingNavigationRequests && DateTime.UtcNow < deadline) Thread.Sleep(1);
                    Check(mailbox.HasPendingNavigationRequests, "real incoming navigation reaches the mailbox while ROT owns a temporary");
                };
                h.Accessor.BeforeAccess = stage =>
                {
                    if (stage == "view.Previous")
                    {
                        Check(f.Balanced(), "scanner fully unwinds all ROT temporaries before any queued navigation invokes Office");
                        CheckRotRoots(h);
                    }
                };
                try
                {
                    h.Call("MaintainBinding");
                    Check((bool)h.Get("rotScanDeferredLastPass") && !h.Accessor.Invoked.Contains("view.Previous"),
                        "scanner only defers and never recursively dispatches the arriving navigation");
                    Check(h.Observe(), "original complete observation runs before serving a deferred navigation");
                    h.Call("ProcessSlideShowExitRequests");
                    Check(client != null && client.Join(3000) && h.Accessor.Invoked.FindAll(value => value == "view.Previous").Count == 1,
                        "queued navigation executes once after clean scanner exit and fresh observation");
                }
                finally
                {
                    h.Accessor.BeforeAccess = null; mailbox.Stop();
                    if (client != null) Check(client.Join(3000), "scanner client reaches a terminal state");
                }
            });
            foreach (bool replace in new[] { false, true })
                WithRotFixture((h, f) =>
                {
                    QueueRotNavigation(h, SlideShowOwnerOperation.Previous); h.Call("MaintainBinding");
                    if (replace)
                    {
                        var replacement = new FakeNode("new-window");
                        foreach (var pair in h.Graph.Window.Properties) replacement.Properties[pair.Key] = pair.Value;
                        replacement.Properties["HWND"] = 0L; h.Graph.Presentation.Properties["SlideShowWindow"] = replacement;
                    }
                    else h.Windows.Properties["Count"] = 0;
                    Check(h.Observe(), "original full observation still runs after deferral");
                    h.Call("ProcessSlideShowExitRequests");
                    Check(!h.Accessor.Invoked.Contains("view.Previous"), "fresh Count0 or replaced show rejects the old queued request after ROT yields");
                });
        }

        public static int Run()
        {
            TestRotYieldOwnership();
            TestRotYieldFairnessAndObservation();
            TestOwnerPollingAndOwnership();
            TestBusyRetryAndMetadata();
            TestFastAndPeriodicMaintenance();
            TestFreshPresentationIdentity();
            TestTopologyInvalidationMatrix();
            TestDelayedReorderDiscovery();
            TestOwnerCommandsAndCallbacks();
            TestOwnerObservesBeforeQueuedCommand();
            TestOwnerCommandPriority();
            TestMailboxBoundedDrain();
            TestMailboxExecution();
            TestEventCleanupOwnership();
            TestCleanupSurvivesDiagnosticFailure();
            TestDisconnectedAndLastDocumentClose();
            TestWindowEvidence();
            TestDiagnosticPreservesErrorInfo();
            TestNavigationPerfDiagnostics();
            TestObservePerfSubstages();
            TestShutdownCleanupPerfDiagnostics();
            return failures;
        }
    }
}
