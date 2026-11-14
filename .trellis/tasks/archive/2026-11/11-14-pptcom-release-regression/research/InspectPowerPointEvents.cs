using System; using System.IO; using System.Reflection; using System.Runtime.InteropServices;
class InspectPowerPointEvents { static int Main(string[] args) {
Assembly.ReflectionOnlyLoadFrom(Path.GetFullPath(args[1]));
var assembly=Assembly.ReflectionOnlyLoadFrom(Path.GetFullPath(args[0]));
var type=assembly.GetType("Microsoft.Office.Interop.PowerPoint.EApplication",true);
foreach(string name in new[]{"PresentationBeforeClose","PresentationClose","PresentationCloseFinal"}) {
var method=type.GetMethod(name); if(method==null) throw new Exception(name);
foreach(var attribute in method.GetCustomAttributesData()) if(attribute.Constructor.DeclaringType==typeof(DispIdAttribute))
Console.WriteLine(name+" DISPID="+attribute.ConstructorArguments[0].Value);
} return 0; }}