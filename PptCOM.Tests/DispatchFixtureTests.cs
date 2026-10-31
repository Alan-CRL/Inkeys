using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

namespace PptCOM.Tests
{
    internal static class DispatchFixtureTests
    {
        private enum Node : uint { Application, Presentation, Window, View, Slides,
            Slide1, Slide2, Slide3, Navigation, ShowWindows, Count }
        private enum Member : int { Item, Name, Caption, FullName, Slides, Count,
            View = 7, Slide, SlideIndex, SlideID, State, Hwnd, Next, Previous,
            Exit, Activate, ActivePresentation, SlideShowWindow, SlideShowWindows,
            Presentation, Active, SlideNavigation, PointerType, LaserPointerEnabled, Visible }
        private enum Profile : uint { WithTypeInfo, NoTypeInfo, TypeInfoFailure, WithEvents }

        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private struct Ledger
        {
            public uint Version, Nodes;
            public long LiveObjects, References, OwnerReleased;
        }
        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private struct NodeLedger
        {
            public long References, Created, Destroyed, Queries, RejectedQueries,
                TypedRejected, AddRefs, Releases, Invokes, NameLookups,
                TypeInfoCounts, TypeInfoGets, ReturnedObjects, Faults,
                NextCalls, PreviousCalls, ExitCalls, ActivateCalls, LastHresult;
        }
        [StructLayout(LayoutKind.Sequential, Pack = 8)]
        private struct EventLedger
        {
            public long Advises, Unadvises, ActiveConnections, SinkQueries, Callbacks,
                SinkAddRefs, SinkReleases, ContainerQueries, PointAcquisitions,
                EmittedArguments, ClearedArguments, LastInvokeHresult,
                LastUnadviseHresult, LastAdviseHresult, LastCancel;
        }

        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate uint MachineDelegate();
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int CreateDelegate(Profile profile, out uint id,
            out IntPtr application, out IntPtr presentation, out IntPtr window);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int SetFaultDelegate(uint id, Node node, Member member, int failure, uint occurrence);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int GetLedgerDelegate(uint id, out Ledger ledger, uint bytes);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int GetNodeLedgerDelegate(uint id, Node node, out NodeLedger ledger, uint bytes);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int OwnerDelegate(uint id);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int ProbeDelegate(uint id, Node node, Member member, out ushort type, out long value);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int ProbeTypeInfoDelegate(uint id, Node node, out uint count, out int typeInfoResult);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int AcquireNodeDelegate(uint id, Node node, out IntPtr value);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int GetEventLedgerDelegate(uint id, out EventLedger ledger, uint bytes);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int SetEventFaultDelegate(uint id, uint operation, int failure, uint occurrence);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int FireEventDelegate(uint id, int member, uint copies, uint includeCancel);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        private sealed class NativeApi
        {
            // 不 FreeLibrary：失败时 CLR 仍可能持有 RCW。零引用由账本证明，卸载由进程退出完成。
            private readonly IntPtr module;
            internal readonly CreateDelegate Create;
            internal readonly SetFaultDelegate SetFault;
            internal readonly GetLedgerDelegate GetLedger;
            internal readonly GetNodeLedgerDelegate GetNodeLedger;
            internal readonly OwnerDelegate ReleaseOwner, Forget;
            internal readonly ProbeDelegate Probe;
            internal readonly ProbeTypeInfoDelegate ProbeTypeInfo;
            internal readonly AcquireNodeDelegate AcquireNode;
            internal readonly GetEventLedgerDelegate GetEventLedger;
            internal readonly SetEventFaultDelegate SetEventFault;
            internal readonly FireEventDelegate FireEvent;

            internal NativeApi(string path)
            {
                module = LoadLibraryEx(path, IntPtr.Zero, 8); // LOAD_WITH_ALTERED_SEARCH_PATH
                if (module == IntPtr.Zero)
                    throw new InvalidOperationException("LoadLibraryEx failed: Win32=" + Marshal.GetLastWin32Error() +
                        ", fixture PE=" + PeMachine(path).ToString("X4"));
                Create = Bind<CreateDelegate>("FixtureCreate");
                SetFault = Bind<SetFaultDelegate>("FixtureSetFault");
                GetLedger = Bind<GetLedgerDelegate>("FixtureGetLedger");
                GetNodeLedger = Bind<GetNodeLedgerDelegate>("FixtureGetNodeLedger");
                ReleaseOwner = Bind<OwnerDelegate>("FixtureReleaseOwner");
                Forget = Bind<OwnerDelegate>("FixtureForget");
                Probe = Bind<ProbeDelegate>("FixtureProbe");
                ProbeTypeInfo = Bind<ProbeTypeInfoDelegate>("FixtureProbeTypeInfo");
                AcquireNode = Bind<AcquireNodeDelegate>("FixtureAcquireNode");
                GetEventLedger = Bind<GetEventLedgerDelegate>("FixtureGetEventLedger");
                SetEventFault = Bind<SetEventFaultDelegate>("FixtureSetEventFault");
                FireEvent = Bind<FireEventDelegate>("FixtureFireEvent");
                uint machine = Bind<MachineDelegate>("FixtureMachine")();
                Check(machine == PeMachine(path), "native fixture machine agrees with DLL PE header");
                Console.WriteLine("Dispatch fixture machine=0x" + machine.ToString("X4"));
            }
            private T Bind<T>(string name) where T : class
            {
                IntPtr address = GetProcAddress(module, name);
                if (address == IntPtr.Zero) throw new MissingMethodException(name);
                return (T)(object)Marshal.GetDelegateForFunctionPointer(address, typeof(T));
            }
        }

        private sealed class Scope : IDisposable
        {
            internal readonly NativeApi Api;
            internal readonly uint Id;
            internal object Application, Presentation, Window;
            private readonly string label;

            internal Scope(NativeApi api, Profile profile, string labelValue)
            {
                Api = api;
                label = labelValue;
                IntPtr app = IntPtr.Zero, presentation = IntPtr.Zero, window = IntPtr.Zero;
                try
                {
                    uint id;
                    Require(api.Create(profile, out id, out app, out presentation, out window));
                    Id = id;
                    Application = Marshal.GetObjectForIUnknown(app);
                    Presentation = Marshal.GetObjectForIUnknown(presentation);
                    Window = Marshal.GetObjectForIUnknown(window);
                    Check(Marshal.IsComObject(Application) && Marshal.IsComObject(Presentation) &&
                        Marshal.IsComObject(Window), label + " uses genuine native COM RCWs");
                    Guid iid = new Guid("91493453-5a91-11cf-8700-00aa0060263b");
                    IntPtr typed;
                    int hr = Marshal.QueryInterface(window, ref iid, out typed);
                    if (typed != IntPtr.Zero) Marshal.Release(typed);
                    Check(hr == unchecked((int)0x80004002) && typed == IntPtr.Zero,
                        label + " rejects actual PowerPoint SlideShowWindow IID");
                    Check(Stats(Node.Window).TypedRejected == 1,
                        label + " native ledger records typed-QI refusal");
                }
                catch
                {
                    if (Id != 0) Dispose();
                    throw;
                }
                finally
                {
                    // 工厂输出和 GetObjectForIUnknown 生成的 RCW 分别各有一笔所有权。
                    if (window != IntPtr.Zero) Marshal.Release(window);
                    if (presentation != IntPtr.Zero) Marshal.Release(presentation);
                    if (app != IntPtr.Zero) Marshal.Release(app);
                }
            }
            internal NodeLedger Stats(Node node)
            {
                NodeLedger value;
                Require(Api.GetNodeLedger(Id, node, out value, (uint)Marshal.SizeOf(typeof(NodeLedger))));
                return value;
            }
            internal void Fault(Node node, Member member, int code, uint occurrence)
            { Require(Api.SetFault(Id, node, member, code, occurrence)); }

            internal EventLedger Events()
            {
                EventLedger value;
                Require(Api.GetEventLedger(Id, out value, (uint)Marshal.SizeOf(typeof(EventLedger))));
                return value;
            }

            internal object Acquire(Node node)
            {
                IntPtr value;
                Require(Api.AcquireNode(Id, node, out value));
                try { return Marshal.GetObjectForIUnknown(value); }
                finally { Marshal.Release(value); }
            }

            internal void CheckTemporaryBaseline(string stage)
            {
                for (Node node = Node.View; node < Node.Count; ++node)
                    Check(Stats(node).References == 1, label + " " + stage + " " + node +
                        " has only fixture-owner reference");
            }

            public void Dispose()
            {
                EventLedger events = Events();
                Check(events.ActiveConnections == 0 && events.SinkAddRefs == events.SinkReleases,
                    label + " event connection and temporary sink references are already balanced");
                ReleaseRoot(ref Window);
                ReleaseRoot(ref Presentation);
                ReleaseRoot(ref Application);
                Require(Api.ReleaseOwner(Id));
                Ledger ledger;
                Require(Api.GetLedger(Id, out ledger, (uint)Marshal.SizeOf(typeof(Ledger))));
                Check(ledger.Version == 1 && ledger.Nodes == (uint)Node.Count && ledger.OwnerReleased == 1,
                    label + " ledger ABI and explicit owner release");
                Check(ledger.LiveObjects == 0 && ledger.References == 0,
                    label + " deterministic release leaves zero live objects/references (live=" +
                    ledger.LiveObjects + ", refs=" + ledger.References + ")");
                for (Node node = 0; node < Node.Count; ++node)
                {
                    NodeLedger stat = Stats(node);
                    Check(stat.Created == 1 && stat.Destroyed == 1 && stat.References == 0 &&
                        stat.Releases == stat.AddRefs + stat.Created,
                        label + " " + node + " native AddRef/Release ledger balances");
                }
                if (ledger.LiveObjects == 0 && ledger.References == 0) Require(Api.Forget(Id));
            }
            private void ReleaseRoot(ref object root)
            {
                object acquired = root;
                root = null;
                if (acquired == null) return;
                try
                {
                    Check(Marshal.ReleaseComObject(acquired) == 0, label + " root RCW single release reaches zero");
                }
                catch (Exception exception)
                { Check(false, label + " root release failed: " + exception.GetType().Name); }
            }
        }

        // 仍由生产适配器完成全部 COM 访问；这层只核对 reader 的 acquisition/Release 配平。
        private sealed class TrackedAccessor : ILateBoundComAccessor
        {
            private readonly Scope scope;
            internal readonly MarshalLateBoundComAccessor Production = new MarshalLateBoundComAccessor();
            internal int Acquired, Released, BorrowedRootReleases;
            internal Exception LastFailure;
            internal TrackedAccessor(Scope scopeValue) { scope = scopeValue; }
            private object Track(object value)
            {
                if (Production.IsComObject(value)) ++Acquired;
                return value;
            }
            public object GetProperty(object target, string name)
            {
                try { return Track(Production.GetProperty(target, name)); }
                catch (Exception error) { LastFailure = error; throw; }
            }
            public object GetItem(object target, int index)
            {
                try { return Track(Production.GetItem(target, index)); }
                catch (Exception error) { LastFailure = error; throw; }
            }
            public void SetProperty(object target, string name, object value) { Production.SetProperty(target, name, value); }
            public void InvokeMethod(object target, string name) { Production.InvokeMethod(target, name); }
            public bool IsComObject(object value) { return Production.IsComObject(value); }
            public void Release(object value)
            {
                if (!Production.IsComObject(value)) return;
                if (ReferenceEquals(value, scope.Application) || ReferenceEquals(value, scope.Presentation) ||
                    ReferenceEquals(value, scope.Window)) ++BorrowedRootReleases;
                ++Released;
                Production.Release(value);
            }
            internal void CheckBalanced(string label)
            {
                Check(Acquired == Released && BorrowedRootReleases == 0,
                    label + " reader releases each temporary once and never releases borrowed root");
            }
        }

        private sealed class FaultCase
        {
            internal Node Node;
            internal Member Member;
            internal uint Occurrence;
            internal string OrdinaryStatus;
            internal FaultCase(Node node, Member member, uint occurrence, string ordinaryStatus)
            { Node = node; Member = member; Occurrence = occurrence; OrdinaryStatus = ordinaryStatus; }
            public override string ToString() { return Node + "." + Member + "#" + Occurrence; }
        }

        private static int failures, cases;
        private static void Check(bool value, string label)
        {
            if (value) return;
            ++failures;
            Console.Error.WriteLine("FAIL dispatch fixture: " + label);
        }
        private static void Require(int hr) { if (hr < 0) Marshal.ThrowExceptionForHR(hr); }
        private static void Case(string label, Action action)
        {
            ++cases;
            try { action(); }
            catch (Exception exception) { Check(false, label + ": " + exception); }
        }

        private static PresentationDescriptorValue Read(Scope scope, TrackedAccessor accessor, out string pageStatus)
        {
            // 本夹具没有真实窗口。HWND 是已批准的包装例外，不用假 HWND/PID 放开生产门。
            return new PresentationDescriptorReader(accessor, delegate { return 0; },
                delegate(object app, object presentation, object window) { return 0; }).ReadShow(
                    scope.Application, scope.Presentation, scope.Window, 17, out pageStatus);
        }

        private static void NativeProbe(Scope scope)
        {
            ushort type;
            long value;
            Require(scope.Api.Probe(scope.Id, Node.Application, Member.Name, out type, out value));
            Check(type == 8 && value > 0, "raw IDispatch Name works independently of ITypeInfo");
            Require(scope.Api.Probe(scope.Id, Node.Slides, Member.Count, out type, out value));
            Check(type == 3 && value == 3, "raw IDispatch scalar works independently of ITypeInfo");
            Require(scope.Api.Probe(scope.Id, Node.Slides, Member.Item, out type, out value));
            Check(type == 9, "raw IDispatch Item returns real dispatch");
            Require(scope.Api.Probe(scope.Id, Node.Window, Member.Activate, out type, out value));
            Check(scope.Stats(Node.Window).ActivateCalls == 1, "raw IDispatch method works independently of ITypeInfo");
            scope.CheckTemporaryBaseline("native probe");
        }

        private static void TestProductionGraph(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithTypeInfo, "production graph"))
            {
                NativeProbe(scope);
                TrackedAccessor accessor = new TrackedAccessor(scope);
                Check(Convert.ToString(accessor.GetProperty(scope.Application, "Name")).Contains("PowerPoint"),
                    "production dynamic reads COM Name with typed QI rejected");
                Check(Convert.ToString(accessor.GetProperty(scope.Presentation, "FullName")) == "C:\\Fixture\\Deck.pptx",
                    "production dynamic reads COM FullName");
                for (int pass = 0; pass < 12; ++pass)
                {
                    string pageStatus;
                    PresentationDescriptorValue value = Read(scope, accessor, out pageStatus);
                    Check(value.status == "StableSlideIds" && pageStatus == "Valid" &&
                        value.currentPage == 2 && value.totalPage == 3 && value.currentSlideId == 202 &&
                        value.slideIds.Length == 3 && value.slideIds[0] == 101 && value.slideIds[2] == 303 &&
                        value.slideShowHwnd == 0 && value.applicationProcessId == 0,
                        "production reader repeated native scan " + pass);
                    accessor.CheckBalanced("repeated scan " + pass);
                    scope.CheckTemporaryBaseline("repeated scan " + pass);
                }
                Check(scope.Stats(Node.View).TypeInfoGets > 0 && scope.Stats(Node.View).Invokes > 0,
                    "production DLR actually inspected native type information and invoked View");
            }
        }

        private static void TestSameIdentityAcquisitions(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithTypeInfo, "same identity"))
            {
                MarshalLateBoundComAccessor accessor = new MarshalLateBoundComAccessor();
                object first = null, second = null;
                try
                {
                    first = accessor.GetProperty(scope.Presentation, "Slides");
                    second = accessor.GetProperty(scope.Presentation, "Slides");
                    Check(ReferenceEquals(first, second), "two COM returns share identity/RCW");
                    accessor.Release(first);
                    first = null;
                    Check(Convert.ToInt32(accessor.GetProperty(second, "Count")) == 3,
                        "first acquisition release preserves independently acquired same RCW");
                }
                finally { accessor.Release(second); accessor.Release(first); }
                scope.CheckTemporaryBaseline("two independent acquisitions");
            }
        }

        private static void TestCommandsAndOptionalProperties(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithTypeInfo, "commands"))
            {
                MarshalLateBoundComAccessor accessor = new MarshalLateBoundComAccessor();
                object view = null, navigation = null, current = null;
                try
                {
                    view = accessor.GetProperty(scope.Window, "View");
                    accessor.InvokeMethod(view, "Next");
                    current = accessor.GetProperty(view, "Slide");
                    Check(Convert.ToInt32(accessor.GetProperty(current, "SlideIndex")) == 3, "native dynamic Next");
                    accessor.Release(current); current = null;
                    accessor.InvokeMethod(view, "Previous");
                    accessor.SetProperty(view, "PointerType", 2);
                    accessor.SetProperty(view, "LaserPointerEnabled", true);
                    Check(Convert.ToInt32(accessor.GetProperty(view, "PointerType")) == 2 &&
                        Convert.ToBoolean(accessor.GetProperty(view, "LaserPointerEnabled")), "native dynamic tool setters");
                    navigation = accessor.GetProperty(scope.Window, "SlideNavigation");
                    accessor.SetProperty(navigation, "Visible", true);
                    accessor.InvokeMethod(scope.Window, "Activate");
                    accessor.InvokeMethod(view, "Exit");
                    Check(scope.Stats(Node.View).NextCalls == 1 && scope.Stats(Node.View).PreviousCalls == 1 &&
                        scope.Stats(Node.View).ExitCalls == 1 && scope.Stats(Node.Window).ActivateCalls == 1,
                        "native command ledger records each side effect once");
                }
                finally { accessor.Release(current); accessor.Release(navigation); accessor.Release(view); }
                TrackedAccessor tracked = new TrackedAccessor(scope);
                string status;
                PresentationDescriptorValue value = Read(scope, tracked, out status);
                Check(status == "EndScreen" && value.status == "StableSlideIds" && value.currentPage == 0 &&
                    value.slideIds.Length == 3, "native end screen reads topology without View.Slide");
                tracked.CheckBalanced("commands and end screen");
                scope.CheckTemporaryBaseline("commands and end screen");
            }
        }

        private static void TestFaults(NativeApi api)
        {
            FaultCase[] stages = {
                new FaultCase(Node.Window, Member.View, 1, "Unavailable"),
                new FaultCase(Node.View, Member.State, 1, "StableSlideIds"),
                new FaultCase(Node.View, Member.Slide, 1, "Unavailable"),
                new FaultCase(Node.Slide2, Member.SlideIndex, 1, "Unavailable"),
                new FaultCase(Node.Slide2, Member.SlideID, 1, "PageIndexFallback"),
                new FaultCase(Node.Presentation, Member.Slides, 1, "Unavailable"),
                new FaultCase(Node.Slides, Member.Count, 1, "Unavailable"),
                new FaultCase(Node.Slides, Member.Item, 1, "PageIndexFallback"),
                new FaultCase(Node.Slides, Member.Item, 2, "PageIndexFallback"),
                new FaultCase(Node.Slides, Member.Item, 3, "PageIndexFallback"),
                new FaultCase(Node.Slide1, Member.SlideIndex, 1, "PageIndexFallback"),
                new FaultCase(Node.Slide1, Member.SlideID, 1, "PageIndexFallback"),
                new FaultCase(Node.Slide2, Member.SlideIndex, 2, "PageIndexFallback"),
                new FaultCase(Node.Slide2, Member.SlideID, 2, "PageIndexFallback"),
                new FaultCase(Node.Slide3, Member.SlideIndex, 1, "PageIndexFallback"),
                new FaultCase(Node.Slide3, Member.SlideID, 1, "PageIndexFallback")
            };
            int[] errors = { unchecked((int)0x8001010A), unchecked((int)0x80010001), unchecked((int)0x800AC472) };
            for (int index = 0; index < stages.Length; ++index)
            {
                FaultCase stage = stages[index];
                for (int busy = 0; busy <= 1; ++busy)
                {
                    bool isBusy = busy == 1;
                    string label = stage + (isBusy ? " busy" : " ordinary failure");
                    Case(label, delegate {
                        using (Scope scope = new Scope(api, Profile.WithTypeInfo, label))
                        {
                            int code = isBusy ? errors[index % errors.Length] : unchecked((int)0x80020003);
                            scope.Fault(stage.Node, stage.Member, code, stage.Occurrence);
                            TrackedAccessor accessor = new TrackedAccessor(scope);
                            string pageStatus;
                            PresentationDescriptorValue value = Read(scope, accessor, out pageStatus);
                            string expected = isBusy ? "TransientBusy" : stage.OrdinaryStatus;
                            if (value.status != expected)
                            {
                                NodeLedger native = scope.Stats(stage.Node);
                                Console.Error.WriteLine("  actual=" + value.status + "/" + pageStatus +
                                    " nativeInvokes=" + native.Invokes + " nativeFaults=" + native.Faults +
                                    " nativeHR=0x" + unchecked((uint)native.LastHresult).ToString("X8") +
                                    " exception=" + (accessor.LastFailure == null ? "none" : accessor.LastFailure.ToString()));
                            }
                            Check(value.status == expected, label + " status");
                            if (isBusy) Check(pageStatus == "Unknown" && value.slideIds.Length == 0,
                                label + " never publishes partial topology or writable current page");
                            Check(scope.Stats(stage.Node).Faults == 1, label + " reaches intended native Invoke stage");
                            accessor.CheckBalanced(label);
                            scope.CheckTemporaryBaseline(label);
                        }
                    });
                }
            }
        }

        private static int ErrorCode(Exception exception)
        {
            ExternalException external = exception as ExternalException;
            return external == null ? Marshal.GetHRForException(exception) : external.ErrorCode;
        }

        private static bool Observe(string name, Action action, out int error)
        {
            try { action(); error = 0; Console.WriteLine("  " + name + " -> success"); return true; }
            catch (Exception exception)
            {
                error = ErrorCode(exception);
                Console.WriteLine("  " + name + " -> " + exception.GetType().FullName + " 0x" + error.ToString("X8"));
                return false;
            }
        }

        private static void TestTypeInfoBoundary(NativeApi api, Profile profile)
        {
            using (Scope scope = new Scope(api, profile, profile.ToString()))
            {
                NativeProbe(scope);
                uint count;
                int typeInfoResult;
                Require(api.ProbeTypeInfo(scope.Id, Node.Application, out count, out typeInfoResult));
                Check(count == (profile == Profile.NoTypeInfo ? 0U : 1U) &&
                    typeInfoResult == (profile == Profile.NoTypeInfo ? unchecked((int)0x8002000B) : unchecked((int)0x80029C4A)),
                    profile + " raw GetTypeInfo shape");
                MarshalLateBoundComAccessor accessor = new MarshalLateBoundComAccessor();
                Console.WriteLine("Dispatch boundary profile=" + profile + ", count=" + count +
                    ", GetTypeInfo=0x" + typeInfoResult.ToString("X8"));
                int error;
                bool property = Observe("production Name", delegate {
                    Check(Convert.ToString(accessor.GetProperty(scope.Application, "Name")).Contains("PowerPoint"),
                        profile + " Name value");
                }, out error);
                bool expectedSuccess = profile == Profile.NoTypeInfo;
                Check(property == expectedSuccess && (expectedSuccess || error == unchecked((int)0x80029C4A)),
                    profile + " production property has explicitly expected capability result");
                bool item = Observe("production Item", delegate {
                    // 直接取得集合 RCW，独立验证 Item，避免前置 Slides getter 掩盖结果。
                    object slides = null, slide = null;
                    try
                    {
                        slides = scope.Acquire(Node.Slides);
                        slide = accessor.GetItem(slides, 1);
                        Check(Convert.ToInt32(accessor.GetProperty(slide, "SlideID")) == 101, profile + " Item value");
                    }
                    finally { accessor.Release(slide); accessor.Release(slides); }
                }, out error);
                Check(item == expectedSuccess && (expectedSuccess || error == unchecked((int)0x80029C4A)),
                    profile + " production collection has explicitly expected capability result");
                bool method = Observe("production Activate", delegate { accessor.InvokeMethod(scope.Window, "Activate"); }, out error);
                Check(method == expectedSuccess && (expectedSuccess || error == unchecked((int)0x80029C4A)),
                    profile + " production method has explicitly expected capability result");
                Check(scope.Stats(Node.Window).ActivateCalls == (expectedSuccess ? 2 : 1),
                    profile + " failed binder cannot execute a hidden side effect");
                TrackedAccessor tracked = new TrackedAccessor(scope);
                string pageStatus;
                PresentationDescriptorValue value = Read(scope, tracked, out pageStatus);
                Check(value.status == (expectedSuccess ? "StableSlideIds" : "Unavailable") &&
                    pageStatus == (expectedSuccess ? "Valid" : "Unknown"),
                    profile + " production reader success or explicit safe unavailable boundary");
                tracked.CheckBalanced(profile.ToString());
                scope.CheckTemporaryBaseline(profile.ToString());
            }
        }

        private static void TestBusyCommandNotRepeated(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithTypeInfo, "busy command"))
            {
                MarshalLateBoundComAccessor accessor = new MarshalLateBoundComAccessor();
                object view = null;
                try
                {
                    view = accessor.GetProperty(scope.Window, "View");
                    scope.Fault(Node.View, Member.Next, unchecked((int)0x8001010A), 1);
                    int error;
                    bool success = Observe("busy production Next", delegate { accessor.InvokeMethod(view, "Next"); }, out error);
                    NodeLedger stat = scope.Stats(Node.View);
                    Check(!success && error == unchecked((int)0x8001010A) && stat.Faults == 1 &&
                        stat.NextCalls == 0 && stat.Invokes == 1, "busy command has no reflection/retry side effect");
                }
                finally { accessor.Release(view); }
                scope.CheckTemporaryBaseline("busy command");
            }
        }

        private static void TestRawEvents(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithEvents, "raw event arguments"))
            {
                ComEventConnection connection = null;
                int notifications = 0;
                try
                {
                    connection = ComEventConnection.Connect(scope.Application, delegate(int member) {
                        Check(member == 2011 || member == 2013 || member == 2014 || member == 2025,
                            "raw event callback only reports numeric DISPID");
                        ++notifications;
                    });
                    EventLedger connected = scope.Events();
                    Check(connected.Advises == 1 && connected.ActiveConnections == 1 &&
                        connected.SinkQueries == 1 && connected.PointAcquisitions == 1,
                        "production event connection owns a real CP acquisition/cookie");
                    NodeLedger windowBefore = scope.Stats(Node.Window);
                    NodeLedger presentationBefore = scope.Stats(Node.Presentation);
                    for (int i = 0; i < 100; ++i)
                        Require(api.FireEvent(scope.Id, 2011, 2, 0));
                    foreach (int member in new[] { 2011, 2013, 2014, 2025 })
                        Require(api.FireEvent(scope.Id, member, 2, member == 2025 ? 1U : 0U));
                    EventLedger fired = scope.Events();
                    Check(notifications == 104 && fired.Callbacks == 104 &&
                        fired.EmittedArguments == 208 && fired.ClearedArguments == 208 && fired.LastCancel == 0,
                        "repeated same-identity IDispatch args and byref cancel are never retained or modified");
                    NodeLedger windowAfter = scope.Stats(Node.Window);
                    NodeLedger presentationAfter = scope.Stats(Node.Presentation);
                    Check(windowAfter.References == windowBefore.References &&
                        presentationAfter.References == presentationBefore.References &&
                        windowAfter.Queries == windowBefore.Queries && presentationAfter.Queries == presentationBefore.Queries &&
                        windowAfter.AddRefs - windowBefore.AddRefs == windowAfter.Releases - windowBefore.Releases &&
                        presentationAfter.AddRefs - presentationBefore.AddRefs == presentationAfter.Releases - presentationBefore.Releases,
                        "raw event sink does not materialize any argument RCW or extra QI");
                    connection.Deactivate();
                    Require(api.FireEvent(scope.Id, 2011, 2, 0));
                    Check(notifications == 104, "deactivated raw sink ignores late callback");
                }
                finally
                {
                    if (connection != null) Check(connection.TryDisconnect(), "raw event connection explicitly unadvises");
                }
                EventLedger disconnected = scope.Events();
                Check(disconnected.ActiveConnections == 0 && disconnected.Unadvises == 1 &&
                    disconnected.SinkAddRefs == disconnected.SinkReleases,
                    "raw event sink and CP release without GC");
                Check(connection != null && connection.TryDisconnect() && scope.Events().Unadvises == 1,
                    "disconnect is idempotent without a second Unadvise");
                Check(Convert.ToString(new MarshalLateBoundComAccessor().GetProperty(scope.Application, "Name")).Contains("PowerPoint"),
                    "CP shares identity with app, but disconnect consumes only its independent acquisition");
            }
        }

        private static void TestRawEventUnadviseRetry(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithEvents, "raw event unadvise retry"))
            {
                ComEventConnection connection = null;
                int notifications = 0;
                try
                {
                    connection = ComEventConnection.Connect(scope.Application, delegate { ++notifications; });
                    Require(api.SetEventFault(scope.Id, 2, unchecked((int)0x8001010A), 1));
                    Check(!connection.TryDisconnect() && connection.LastError == unchecked((int)0x8001010A),
                        "failed real Unadvise retains original HRESULT and retry state");
                    EventLedger failed = scope.Events();
                    Check(failed.ActiveConnections == 1 && failed.Unadvises == 1 && failed.PointAcquisitions == 1,
                        "Unadvise failure keeps the same owned CP and cookie");
                    Require(api.FireEvent(scope.Id, 2013, 2, 0));
                    Check(notifications == 0, "failed unsubscribe still deactivates late notifications");
                    Check(connection.TryDisconnect(), "explicit owner retry disconnects original cookie");
                    EventLedger recovered = scope.Events();
                    Check(recovered.ActiveConnections == 0 && recovered.Unadvises == 2 &&
                        recovered.PointAcquisitions == 1 && recovered.SinkAddRefs == recovered.SinkReleases,
                        "Unadvise retry releases each sink/CP acquisition once");
                }
                finally { if (connection != null) Check(connection.TryDisconnect(), "retry cleanup is complete"); }
            }
        }

        private static void TestRawEventAdviseFailure(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithEvents, "raw event advise failure"))
            {
                Require(api.SetEventFault(scope.Id, 1, unchecked((int)0x80010001), 1));
                ComEventConnection connection = null;
                int failure = 0;
                try { connection = ComEventConnection.Connect(scope.Application, delegate { }); }
                catch (Exception exception) { failure = ErrorCode(exception); }
                finally { if (connection != null) Check(connection.TryDisconnect(), "unexpected successful Advise cleanup"); }
                Check(failure == unchecked((int)0x80010001), "failed real Advise preserves HRESULT");
                EventLedger failed = scope.Events();
                Check(failed.Advises == 1 && failed.ActiveConnections == 0 && failed.SinkQueries == 0 &&
                    failed.PointAcquisitions == 1, "failed Advise releases its independent CP acquisition");
                try
                {
                    connection = ComEventConnection.Connect(scope.Application, delegate { });
                    Require(api.FireEvent(scope.Id, 2025, 2, 1));
                }
                finally { if (connection != null) Check(connection.TryDisconnect(), "Advise recovery explicitly disconnects"); }
                Check(scope.Events().Advises == 2 && scope.Events().Unadvises == 1 &&
                    scope.Events().PointAcquisitions == 2, "fresh registration works after failed Advise");
            }
        }

        private static void TestEventCapabilityUnavailable(NativeApi api)
        {
            using (Scope scope = new Scope(api, Profile.WithTypeInfo, "no event capability"))
            {
                ComEventConnection connection = null;
                bool refused = false;
                try { connection = ComEventConnection.Connect(scope.Application, delegate { }); }
                catch (InvalidCastException) { refused = true; }
                catch (COMException exception) { refused = exception.ErrorCode == unchecked((int)0x80004002); }
                finally { if (connection != null) Check(connection.TryDisconnect(), "unexpected event capability cleanup"); }
                Check(refused, "event wrapper rejects absent CPC capability");
                TrackedAccessor accessor = new TrackedAccessor(scope);
                string pageStatus;
                Check(Read(scope, accessor, out pageStatus).status == "StableSlideIds" && pageStatus == "Valid",
                    "unavailable event wrapper leaves production dynamic polling readable");
                accessor.CheckBalanced("no event capability");
                scope.CheckTemporaryBaseline("no event capability");
            }
        }

        private static ushort PeMachine(string path)
        {
            using (BinaryReader reader = new BinaryReader(File.OpenRead(path)))
            {
                if (reader.ReadUInt16() != 0x5a4d) throw new InvalidDataException("Missing MZ: " + path);
                reader.BaseStream.Position = 0x3c;
                int offset = reader.ReadInt32();
                reader.BaseStream.Position = offset;
                if (reader.ReadUInt32() != 0x00004550) throw new InvalidDataException("Missing PE: " + path);
                return reader.ReadUInt16();
            }
        }

        internal static int Run(string path)
        {
            failures = cases = 0;
            Case("load and runtime", delegate {
                if (!Path.IsPathRooted(path)) throw new ArgumentException("Fixture DLL path must be absolute");
                string clr = string.Empty;
                using (Process process = Process.GetCurrentProcess())
                    foreach (ProcessModule module in process.Modules)
                        if (string.Equals(module.ModuleName, "clr.dll", StringComparison.OrdinalIgnoreCase)) clr = module.FileName;
                if (clr.Length == 0) throw new InvalidOperationException("Cannot identify loaded .NET Framework CLR");
                Console.WriteLine("Dispatch runtime=" + Environment.Version + ", pointerBytes=" + IntPtr.Size +
                    ", CLR PE=0x" + PeMachine(clr).ToString("X4") + ", clr=" + clr);
                Check(PeMachine(path) == PeMachine(clr), "fixture target matches loaded CLR, not PowerShell/OS architecture");
                NativeApi api = new NativeApi(path);
                Case("production graph", delegate { TestProductionGraph(api); });
                Case("same identity", delegate { TestSameIdentityAcquisitions(api); });
                Case("commands", delegate { TestCommandsAndOptionalProperties(api); });
                TestFaults(api);
                Case("NoTypeInfo", delegate { TestTypeInfoBoundary(api, Profile.NoTypeInfo); });
                Case("TypeInfoFailure", delegate { TestTypeInfoBoundary(api, Profile.TypeInfoFailure); });
                Case("busy command", delegate { TestBusyCommandNotRepeated(api); });
                Case("raw event arguments", delegate { TestRawEvents(api); });
                Case("raw event unadvise retry", delegate { TestRawEventUnadviseRetry(api); });
                Case("raw event advise failure", delegate { TestRawEventAdviseFailure(api); });
                Case("event capability unavailable", delegate { TestEventCapabilityUnavailable(api); });
            });
            Console.WriteLine("Native dispatch fixture: " + cases + " cases, " + failures +
                " failures; deterministic RCW/owner release checked without GC or DLL unload");
            return failures;
        }
    }
}
