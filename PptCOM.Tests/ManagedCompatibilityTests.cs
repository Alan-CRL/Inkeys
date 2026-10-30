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
                Check(ItemReadCount(h) == 0 && Convert.ToInt64(descriptor["slideShowHwnd"]) == 0 &&
                    Convert.ToInt32(descriptor["applicationProcessId"]) == 0,
                    "fast descriptor never inherits an old show HWND or process identity");
                Check(h.Accessor.IsBalanced() || h.Outstanding("application") == 1,
                    "fast/full interleaving keeps the ledger available for final owner cleanup");
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

        public static int Run()
        {
            TestOwnerPollingAndOwnership();
            TestBusyRetryAndMetadata();
            TestFastAndPeriodicMaintenance();
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
            return failures;
        }
    }
}
