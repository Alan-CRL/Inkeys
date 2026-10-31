using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Threading;
using System.Web.Script.Serialization;

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

            public ServerHarness()
            {
                Server = new PptCOMServer(Accessor);
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
                Check(GetErrorInfo(0, out observed) == 0 && observed == marker,
                    "diagnostic neither replaces nor clears existing thread IErrorInfo");
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

        public static int Run()
        {
            TestOwnerPollingAndOwnership();
            TestBusyRetryAndMetadata();
            TestOwnerCommandsAndCallbacks();
            TestMailboxExecution();
            TestEventCleanupOwnership();
            TestCleanupSurvivesDiagnosticFailure();
            TestDisconnectedAndLastDocumentClose();
            TestWindowEvidence();
            TestDiagnosticPreservesErrorInfo();
            return failures;
        }
    }
}
