using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Web.Script.Serialization;

namespace PptCOM.Tests
{
    internal static class SlideShowSessionTests
    {
        private static int failures;

        private static void Check(bool value, string name)
        {
            if (value) return;
            ++failures;
            Console.Error.WriteLine("FAIL " + name);
        }

        private static Dictionary<string, object> Json(string json)
        {
            return new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);
        }

        private static PresentationDescriptorValue Descriptor(FakeGraph graph, FakeAccessor accessor,
            long binding, out string pageStatus)
        {
            return new PresentationDescriptorReader(accessor, delegate { return 42; }).ReadShow(
                graph.Application, graph.Presentation, graph.Window, binding, out pageStatus);
        }

        private static void TestStateReadAndOwnership()
        {
            for (int viewState = 1; viewState <= 5; ++viewState)
            {
                FakeGraph graph = new FakeGraph("Microsoft PowerPoint");
                ((FakeNode)graph.Window.Properties["View"]).Properties["State"] = viewState;
                FakeAccessor accessor = new FakeAccessor();
                string pageStatus;
                PresentationDescriptorValue descriptor = Descriptor(graph, accessor, 7, out pageStatus);
                Check(pageStatus == (viewState == 5 ? "EndScreen" : "Valid"), "View.State " + viewState);
                if (viewState == 5)
                {
                    Check(descriptor.status == "Unavailable" && descriptor.currentPage == 0 &&
                        descriptor.totalPage == 3 && descriptor.slideShowHwnd == 100,
                        "end screen retains v1 nonnegative page/identity and validated total");
                    Check(!accessor.Acquired.ContainsKey("current"), "done does not require View.Slide");
                }
                else Check(descriptor.status == "StableSlideIds", "black/white/paused keeps valid page");
                Check(accessor.IsBalanced() && accessor.ReleaseCount("window") == 0,
                    "state read balances temporary COM and borrows window");
            }

            FakeGraph missing = new FakeGraph("WPS Presentation");
            string status;
            FakeAccessor missingAccessor = new FakeAccessor();
            Check(Descriptor(missing, missingAccessor, 7, out status).status == "StableSlideIds" &&
                status == "Valid", "missing optional State keeps self-consistent WPS page");
            ((FakeNode)missing.Window.Properties["View"]).Properties["State"] = 999;
            Descriptor(missing, missingAccessor, 7, out status);
            Check(status == "Unknown", "unrecognized state never inferred as done");

            foreach (string stage in new[] { "view.State", "window.View", "view.Slide", "slides.Count" })
            {
                FakeGraph graph = new FakeGraph("Microsoft PowerPoint");
                ((FakeNode)graph.Window.Properties["View"]).Properties["State"] = 1;
                FakeAccessor accessor = new FakeAccessor
                {
                    FailureStage = stage, Failure = new COMException("busy", unchecked((int)0x8001010A))
                };
                Check(Descriptor(graph, accessor, 7, out status).status == "TransientBusy" && status == "Unknown",
                    stage + " busy cannot create end screen");
                Check(accessor.IsBalanced(), stage + " balanced busy releases");
            }

            FakeGraph done = new FakeGraph("Microsoft PowerPoint");
            ((FakeNode)done.Window.Properties["View"]).Properties["State"] = 5;
            FakeAccessor failedCount = new FakeAccessor
            {
                FailureStage = "slides.Count", Failure = new MissingMemberException("Count")
            };
            Descriptor(done, failedCount, 7, out status);
            Check(status == "Unknown" && failedCount.IsBalanced(), "done with unreadable total remains unknown");
        }

        private static void TestPublication()
        {
            SlideShowSessionCache cache = new SlideShowSessionCache();
            Dictionary<string, object> initial = Json(cache.GetSince(0));
            Check((string)initial["lifecycle"] == "Unknown", "initial lifecycle is not false exit");
            SlideShowObservationStamp pending = cache.Begin(7, false);
            Check(!cache.Matches(pending.Session) && !cache.ObserveIfCurrent(pending, 7, true, 100),
                "owner cannot activate Begin before window installation completes");
            cache.PageChanged();
            cache.FinishBegin(pending);
            Check(cache.Matches(pending.Session), "page event during Begin does not permanently lock session");
            SlideShowObservationStamp staleBegin = cache.Begin(7, false);
            cache.Observe(7, false, 0);
            cache.FinishBegin(staleBegin);
            Check(cache.Capture().Lifecycle == "Inactive", "old Begin completion cannot undo End");
            cache.Begin(7);
            SlideShowObservationStamp first = cache.Capture();
            FakeGraph graph = new FakeGraph("Microsoft PowerPoint");
            string pageStatus;
            PresentationDescriptorValue descriptor = Descriptor(graph, new FakeAccessor(), 7, out pageStatus);
            Check(cache.Publish(first, descriptor, pageStatus), "owner publishes accepted descriptor");
            Dictionary<string, object> active = Json(cache.GetSince(0));
            long revision = Convert.ToInt64(active["stateRevision"]);
            Check(Convert.ToInt64(active["showSessionRevision"]) == first.Session &&
                (string)active["pageStatus"] == "Valid", "envelope carries exact accepted show/page");
            Check(cache.GetSince(revision) == string.Empty, "unchanged poll is empty");
            Check(cache.Publish(first, descriptor.Clone(), pageStatus) && cache.GetSince(revision) == string.Empty,
                "identical owner maintenance does not advance state revision");

            cache.PageChanged();
            Check(!cache.Publish(first, descriptor, pageStatus), "old read cannot publish after page event");
            SlideShowObservationStamp changed = cache.Capture();
            ((FakeNode)((FakeNode)graph.Window.Properties["View"]).Properties["Slide"]).Properties["SlideIndex"] = 3;
            ((FakeNode)((FakeNode)graph.Window.Properties["View"]).Properties["Slide"]).Properties["SlideID"] = 303;
            descriptor = Descriptor(graph, new FakeAccessor(), 7, out pageStatus);
            Check(cache.Publish(changed, descriptor, pageStatus), "new owner page publishes");
            Check(!string.IsNullOrEmpty(cache.GetSince(revision)), "new page wakes revision consumer");

            cache.Observe(7, null, 0);
            SlideShowObservationStamp unknown = cache.Capture();
            cache.Publish(unknown, descriptor, "Valid");
            Dictionary<string, object> busy = Json(cache.GetSince(0));
            Check((string)busy["lifecycle"] == "Unknown" && (string)busy["pageStatus"] == "Unknown" &&
                unknown.Session == first.Session && !cache.Matches(first.Session),
                "transient lifecycle suspends writes/exit without inventing a new show");
            cache.Observe(7, true, 100);
            Check(cache.Capture().Session == first.Session, "recovered same show keeps its epoch");

            cache.Observe(7, false, 0);
            SlideShowObservationStamp ended = cache.Capture();
            Check(!cache.Publish(changed, descriptor, "Valid"), "read finishing after End is rejected");
            Check(!cache.ObserveIfCurrent(changed, 7, true, 100),
                "late positive owner probe cannot undo End");
            cache.Publish(ended, descriptor, "Valid");
            Check((string)Json(cache.GetSince(0))["lifecycle"] == "Inactive", "confirmed end published");
            cache.Begin(7);
            Check(cache.Capture().Session > first.Session && !cache.Matches(first.Session),
                "same binding/window new Begin rejects old confirmation");
            cache.Observe(8, null, 0);
            SlideShowObservationStamp rebound = cache.Capture();
            Check(!cache.Publish(rebound, descriptor, "Valid"), "descriptor from another binding rejected");
            cache.Publish(rebound, PresentationDescriptorValue.CreateStatus("Unavailable", 8), "Unknown");
            Dictionary<string, object> binding = Json(cache.GetSince(0));
            Check(Convert.ToInt64(binding["bindingRevision"]) ==
                Convert.ToInt64(((Dictionary<string, object>)binding["descriptor"])["bindingRevision"]),
                "unknown envelope and descriptor binding remain coherent");
        }

        private static void TestExitExecution()
        {
            SlideShowSessionCache cache = new SlideShowSessionCache();
            cache.Begin(1);
            cache.Observe(1, true, 100);
            long session = cache.Capture().Session;
            FakeGraph graph = new FakeGraph("Microsoft PowerPoint");
            FakeAccessor accessor = new FakeAccessor();
            Check(SlideShowSessionExit.Execute(new SlideShowExitRequest(session), cache, graph.Window, accessor) == 1 &&
                accessor.Invoked.Count == 1 && accessor.IsBalanced(), "matched exit uses/release captured View");
            Check(accessor.ReleaseCount("window") == 0, "exit never releases borrowed root");
            cache.Begin(1);
            accessor = new FakeAccessor();
            Check(SlideShowSessionExit.Execute(new SlideShowExitRequest(session), cache, graph.Window, accessor) == 0 &&
                accessor.Acquired.Count == 0, "queued old confirmation never acquires new show");

            cache.Observe(1, true, 100);
            session = cache.Capture().Session;
            accessor = new FakeAccessor { AfterAcquire = delegate { cache.Begin(1); } };
            Check(SlideShowSessionExit.Execute(new SlideShowExitRequest(session), cache, graph.Window, accessor) == 0 &&
                accessor.Invoked.Count == 0 && accessor.IsBalanced(),
                "Begin pumped during View acquisition prevents actual Exit");
            cache.Observe(1, true, 100);
            session = cache.Capture().Session;
            foreach (string stage in new[] { "window.HWND", "window.View", "view.Exit" })
            {
                accessor = new FakeAccessor { FailureStage = stage, Failure = new COMException("failed") };
                Check(SlideShowSessionExit.Execute(new SlideShowExitRequest(session), cache, graph.Window, accessor) == -1 &&
                    accessor.IsBalanced(), stage + " failure closes request and balances COM");
            }
        }

        private static void TestWindowGuard()
        {
            SlideShowSessionCache cache = new SlideShowSessionCache();
            cache.Begin(7);
            long session = cache.Capture().Session;
            FakeGraph graph = new FakeGraph("WPS Presentation");
            FakeAccessor accessor = new FakeAccessor();
            Check(SlideShowSessionExit.Execute(new SlideShowExitRequest(session), cache, graph.Window, accessor) == 0,
                "unknown HWND cannot execute guarded exit");
            string status;
            PresentationDescriptorValue first = Descriptor(graph, accessor, 7, out status);
            cache.Publish(cache.Capture(), first, status);
            Check(cache.Capture().Window == 100 && cache.Capture().Session == session,
                "accepted late-bound descriptor adopts initial HWND without restarting show");
            graph.Window.Properties["HWND"] = 101L;
            Check(SlideShowSessionExit.Execute(new SlideShowExitRequest(session), cache, graph.Window, accessor) == 0 &&
                accessor.Invoked.Count == 0, "changed HWND rejected at actual exit execution");
            PresentationDescriptorValue replacement = Descriptor(graph, accessor, 7, out status);
            cache.Publish(cache.Capture(), replacement, status);
            Check(cache.Capture().Session != session && cache.Capture().Window == 101,
                "poll-only replacement window advances session epoch");
        }

        private static void TestNativeObservationGap()
        {
            SlideShowSessionCache cache = new SlideShowSessionCache();
            cache.Begin(7);
            FakeGraph graph = new FakeGraph("Microsoft PowerPoint");
            FakeAccessor accessor = new FakeAccessor();
            string status;
            PresentationDescriptorValue descriptor = Descriptor(graph, accessor, 7, out status);
            cache.Publish(cache.Capture(), descriptor, status);
            long oldSession = cache.Capture().Session;
            string nativeSnapshot = cache.GetSince(0);
            cache.Observe(7, false, 0);
            cache.Begin(7); // 同 binding、同 HWND，且 native 尚未轮询新的 owner 快照。
            Check(cache.GetSince(0) == nativeSnapshot, "event callbacks leave serialization to owner");
            Check(SlideShowSessionExit.Execute(new SlideShowExitRequest(oldSession), cache, graph.Window, accessor) == 0 &&
                accessor.Invoked.Count == 0, "old native confirmation cannot exit rapid same-window new show");
            cache.Publish(cache.Capture(), descriptor, status);
            Check(Convert.ToInt64(Json(cache.GetSince(0))["showSessionRevision"]) != oldSession,
                "next owner publication exposes latest epoch after merged End/Begin");
            cache.Observe(7, false, 0);
            cache.InvalidateBinding(8);
            SlideShowObservationStamp cleaned = cache.Capture();
            cache.Publish(cleaned, PresentationDescriptorValue.CreateStatus("Unavailable", 8), "Unknown");
            Check(cleaned.Lifecycle == "Inactive" && (string)Json(cache.GetSince(0))["lifecycle"] == "Inactive",
                "cleanup preserves confirmed end when native missed callback interval");
        }

        private static void TestOwnerMailbox()
        {
            SlideShowOwnerMailbox mailbox = new SlideShowOwnerMailbox();
            mailbox.Start();
            mailbox.MaintenanceCompleted();
            int before = mailbox.MaintenanceWaitMilliseconds;
            mailbox.Wake();
            Check(mailbox.Wait(0), "event refresh wakes existing owner");
            Check(mailbox.MaintenanceWaitMilliseconds <= before && mailbox.MaintenanceWaitMilliseconds > 0,
                "event wake does not advance slow ROT maintenance");
            int result = -2;
            Thread client = new Thread(delegate() { result = mailbox.RequestExit(12, 2000); });
            client.Start();
            Check(mailbox.Wait(1000), "exit request wakes owner");
            int executed = 0;
            mailbox.ProcessExits(delegate(SlideShowExitRequest request)
            {
                ++executed;
                Check(request.Session == 12, "mailbox preserves expected session");
                return 1;
            });
            Check(client.Join(2000) && result == 1 && executed == 1,
                "caller receives actual owner result, not queue acknowledgment");

            Check(mailbox.RequestExit(12, 1) == -1, "unserviced request times out");
            mailbox.ProcessExits(delegate { ++executed; return 1; });
            Check(executed == 1, "timed-out request cannot execute on a later wake");
            mailbox.Wait(0);
            client = new Thread(delegate() { result = mailbox.RequestExit(12, 2000); });
            client.Start();
            Check(mailbox.Wait(1000), "shutdown test request reaches owner");
            mailbox.Stop();
            Check(client.Join(2000) && result == -1, "owner shutdown releases waiting command");
            Check(mailbox.RequestExit(12, 1) == -1, "stopped owner rejects new requests");
        }

        private static void TestAbi()
        {
            string[] oldNames = { "Initialization", "CheckCOM", "PptComService", "SlideNameIndex", "GetPptHwnd",
                "GetSlideShowAnnotationTool", "ExitSlideShowAnnotationTool", "NextSlideShow", "PreviousSlideShow",
                "EndSlideShow", "ViewSlideShow", "ActivateSildeShowWindow", "SetConsoleOutputEnabled", "GetPresentationDescriptor" };
            MethodInfo[] methods = typeof(IPptCOMServer).GetMethods();
            Array.Sort(methods, delegate(MethodInfo a, MethodInfo b) { return a.MetadataToken.CompareTo(b.MetadataToken); });
            Check(methods.Length == oldNames.Length, "old interface method count unchanged");
            for (int index = 0; index < Math.Min(methods.Length, oldNames.Length); ++index)
                Check(methods[index].Name == oldNames[index], "old COM slot " + index);
            Check(typeof(IPptCOMServer).GUID == new Guid("65F6E9C1-63EC-4003-B89F-8F425A3C2FEA") &&
                typeof(PptCOMServer).GUID == new Guid("C44270BE-9A52-400F-AD7C-ED42050A77D8"), "old IIDs unchanged");
            PptCOMServer server = new PptCOMServer();
            Check(server is IPptCOMSessionState && Json(server.GetPresentationDescriptor()).Count == 12,
                "new separately queried interface preserves old descriptor schema");
            Dictionary<string, object> initial = Json(server.GetSlideShowState(0));
            Check(server.GetSlideShowState(Convert.ToInt64(initial["stateRevision"])) == string.Empty &&
                server.EndSlideShowIfSession(1) == 0, "public cache getter and unavailable exit require no Office");
        }

        public static int Run()
        {
            TestStateReadAndOwnership();
            TestPublication();
            TestExitExecution();
            TestWindowGuard();
            TestNativeObservationGap();
            TestOwnerMailbox();
            TestAbi();
            return failures;
        }
    }
}
