using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace PptCOM.Tests
{
    internal sealed class FakeNode
    {
        public FakeNode(string name) { Name = name; }
        public string Name { get; private set; }
        public Dictionary<string, object> Properties = new Dictionary<string, object>();
        public List<FakeNode> Items = new List<FakeNode>();
    }

    internal sealed class FakeAccessor : ILateBoundComAccessor
    {
        public string FailureStage;
        public Exception Failure;
        public readonly Dictionary<string, int> Acquired = new Dictionary<string, int>();
        public readonly Dictionary<string, int> Released = new Dictionary<string, int>();
        public readonly List<string> ReleaseOrder = new List<string>();

        private void ThrowIfRequested(string stage)
        {
            if (FailureStage == stage) throw Failure;
        }

        private object Acquire(object value)
        {
            FakeNode node = value as FakeNode;
            if (node == null) return value;
            int count;
            Acquired.TryGetValue(node.Name, out count);
            Acquired[node.Name] = count + 1;
            return node;
        }

        public object GetProperty(object target, string name)
        {
            FakeNode node = (FakeNode)target;
            ThrowIfRequested(node.Name + "." + name);
            object value;
            if (!node.Properties.TryGetValue(name, out value))
                throw new MissingMemberException(node.Name, name);
            return Acquire(value);
        }

        public object GetItem(object collection, int oneBasedIndex)
        {
            FakeNode node = (FakeNode)collection;
            ThrowIfRequested(node.Name + ".Item:" + oneBasedIndex);
            return Acquire(node.Items[oneBasedIndex - 1]);
        }

        public bool IsComObject(object value) { return value is FakeNode; }

        public void Release(object value)
        {
            FakeNode node = value as FakeNode;
            if (node == null) return;
            int acquired;
            Acquired.TryGetValue(node.Name, out acquired);
            int released;
            Released.TryGetValue(node.Name, out released);
            if (released >= acquired)
                throw new InvalidOperationException("release without acquisition: " + node.Name);
            Released[node.Name] = released + 1;
            ReleaseOrder.Add(node.Name);
        }

        public bool IsBalanced()
        {
            foreach (KeyValuePair<string, int> pair in Acquired)
            {
                int released;
                Released.TryGetValue(pair.Key, out released);
                if (released != pair.Value) return false;
            }
            return true;
        }

        public int ReleaseCount(string name)
        {
            int count;
            Released.TryGetValue(name, out count);
            return count;
        }
    }

    internal sealed class FakeGraph
    {
        public readonly FakeNode Application = new FakeNode("application");
        public readonly FakeNode Presentation = new FakeNode("presentation");
        public readonly FakeNode Window = new FakeNode("window");

        public FakeGraph(string applicationName)
        {
            Application.Properties["Name"] = applicationName;
            Application.Properties["Caption"] = applicationName;
            Presentation.Properties["FullName"] = "C:\\Lessons\\Deck.pptx";
            Presentation.Properties["Name"] = "Deck.pptx";
            Window.Properties["HWND"] = 100L;

            FakeNode view = new FakeNode("view");
            FakeNode current = Slide("current", 2, 202);
            view.Properties["Slide"] = current;
            Window.Properties["View"] = view;

            FakeNode slides = new FakeNode("slides");
            slides.Properties["Count"] = 3;
            slides.Items.Add(Slide("item1", 1, 101));
            slides.Items.Add(Slide("item2", 2, 202));
            slides.Items.Add(Slide("item3", 3, 303));
            Presentation.Properties["Slides"] = slides;
        }

        private static FakeNode Slide(string name, int index, int id)
        {
            FakeNode slide = new FakeNode(name);
            slide.Properties["SlideIndex"] = index;
            slide.Properties["SlideID"] = id;
            return slide;
        }
    }

    internal static class Program
    {
        private static int failures;

        private static void Check(bool condition, string name)
        {
            if (condition) return;
            ++failures;
            Console.Error.WriteLine("FAIL " + name);
        }

        private static PresentationDescriptorValue Read(FakeGraph graph,
            FakeAccessor accessor)
        {
            PresentationDescriptorReader reader = new PresentationDescriptorReader(
                accessor, delegate { return 42; });
            return reader.Read(graph.Application, graph.Presentation, graph.Window, 7);
        }

        private static void TestSuccess(string applicationName, string expectedProvider)
        {
            FakeGraph graph = new FakeGraph(applicationName);
            FakeAccessor accessor = new FakeAccessor();
            PresentationDescriptorValue value = Read(graph, accessor);
            Check(value.status == "StableSlideIds" && value.provider == expectedProvider &&
                value.currentPage == 2 && value.currentSlideId == 202 &&
                value.slideIds.Length == 3, applicationName + " stable descriptor");
            for (int index = 0; index < 100; ++index)
                Check(Read(graph, accessor).status == "StableSlideIds",
                    applicationName + " repeated scan remains stable");
            Check(accessor.IsBalanced(), applicationName + " temporary releases balanced");
            Check(accessor.ReleaseCount("application") == 0 &&
                accessor.ReleaseCount("presentation") == 0 &&
                accessor.ReleaseCount("window") == 0,
                applicationName + " borrowed roots never released");
            Check(accessor.ReleaseOrder.IndexOf("current") <
                accessor.ReleaseOrder.IndexOf("view"),
                applicationName + " current Slide released before View");
            Check(accessor.ReleaseOrder[accessor.ReleaseOrder.Count - 1] == "slides",
                applicationName + " Slides released after all Item acquisitions");
        }

        private static void TestFallbackAndFailures()
        {
            FakeGraph missingId = new FakeGraph("WPS Presentation");
            FakeAccessor missingAccessor = new FakeAccessor
            {
                FailureStage = "current.SlideID",
                Failure = new MissingMemberException("SlideID")
            };
            PresentationDescriptorValue fallback = Read(missingId, missingAccessor);
            Check(fallback.status == "PageIndexFallback" &&
                !fallback.currentSlideId.HasValue && fallback.slideIds.Length == 0,
                "missing SlideID produces explicit page-index fallback");
            Check(missingAccessor.IsBalanced(), "fallback releases all temporaries");

            string[] failureStages = {
                "window.View", "view.Slide", "current.SlideIndex",
                "presentation.Slides", "slides.Count", "slides.Item:2", "item2.SlideID"
            };
            foreach (string stage in failureStages)
            {
                FakeGraph graph = new FakeGraph("Microsoft PowerPoint");
                FakeAccessor accessor = new FakeAccessor
                {
                    FailureStage = stage,
                    Failure = new MissingMemberException(stage)
                };
                PresentationDescriptorValue value = Read(graph, accessor);
                Check(value.status == (stage == "window.View" || stage == "view.Slide" ||
                    stage == "current.SlideIndex" || stage == "presentation.Slides" ||
                    stage == "slides.Count" ? "Unavailable" : "PageIndexFallback"),
                    stage + " classified without partial topology");
                Check(value.slideIds.Length == 0, stage + " never publishes partial IDs");
                Check(accessor.IsBalanced(), stage + " releases acquired temporaries");
            }

            string[] busyStages = {
                "application.Name", "presentation.FullName", "window.HWND",
                "window.View", "view.Slide", "current.SlideIndex",
                "current.SlideID", "presentation.Slides", "slides.Count",
                "slides.Item:2", "item2.SlideIndex", "item2.SlideID"
            };
            foreach (string stage in busyStages)
            {
                FakeGraph busyGraph = new FakeGraph("Microsoft PowerPoint");
                FakeAccessor busyAccessor = new FakeAccessor
                {
                    FailureStage = stage,
                    Failure = new COMException("busy", unchecked((int)0x8001010A))
                };
                Check(Read(busyGraph, busyAccessor).status == "TransientBusy",
                    stage + " busy HRESULT is not downgraded to fallback");
                Check(busyAccessor.IsBalanced(),
                    stage + " busy path releases acquired temporaries");
            }

            FakeGraph scalarComGraph = new FakeGraph("WPS Presentation");
            FakeAccessor scalarComAccessor = new FakeAccessor();
            FakeNode scalarCom = new FakeNode("scalarCom");
            ((FakeNode)((FakeNode)scalarComGraph.Window.Properties["View"])
                .Properties["Slide"]).Properties["SlideID"] = scalarCom;
            PresentationDescriptorValue scalarFallback = Read(
                scalarComGraph, scalarComAccessor);
            Check(scalarFallback.status == "PageIndexFallback" &&
                scalarComAccessor.ReleaseCount("scalarCom") == 1 &&
                scalarComAccessor.IsBalanced(),
                "unexpected COM-valued scalar is rejected and released once");

            FakeGraph oversizedGraph = new FakeGraph("Microsoft PowerPoint");
            FakeAccessor oversizedAccessor = new FakeAccessor();
            ((FakeNode)oversizedGraph.Presentation.Properties["Slides"])
                .Properties["Count"] = 10001;
            PresentationDescriptorValue oversized = Read(oversizedGraph,
                oversizedAccessor);
            Check(oversized.status == "Unavailable" &&
                oversized.slideIds.Length == 0,
                "oversized damaged topology is rejected before allocation");
            Check(oversizedAccessor.IsBalanced(),
                "oversized topology releases Slides");

            FakeGraph oversizedIdentityGraph = new FakeGraph("Microsoft PowerPoint");
            FakeAccessor oversizedIdentityAccessor = new FakeAccessor();
            oversizedIdentityGraph.Presentation.Properties["FullName"] =
                new string('x', 32769);
            oversizedIdentityGraph.Presentation.Properties["Name"] = string.Empty;
            PresentationDescriptorValue oversizedIdentity = Read(
                oversizedIdentityGraph, oversizedIdentityAccessor);
            Check(oversizedIdentity.status == "Unavailable",
                "oversized damaged identity is not cached");
            Check(oversizedIdentityAccessor.IsBalanced() &&
                oversizedIdentityAccessor.ReleaseCount("presentation") == 0,
                "identity rejection does not release borrowed roots");
        }

        public static int Main()
        {
            TestSuccess("Microsoft PowerPoint", "PowerPoint");
            TestSuccess("WPS Presentation", "Wps");
            TestFallbackAndFailures();
            if (failures != 0)
            {
                Console.Error.WriteLine("FAILED count=" + failures);
                return 1;
            }
            Console.WriteLine("PASS PptCOM presentation descriptor ownership");
            return 0;
        }
    }
}
