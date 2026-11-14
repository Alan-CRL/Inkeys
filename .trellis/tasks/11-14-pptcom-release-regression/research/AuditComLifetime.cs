using System;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Collections.Generic;
using System.Runtime.InteropServices;
class AuditComLifetime {
    const BindingFlags F = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static void ReportGc(string path) {
        Assembly a = Assembly.ReflectionOnlyLoad(File.ReadAllBytes(path));
        MethodInfo method = a.GetType("PptCOM.PptCOMServer",true).GetMethod("FullCleanup",F);
        byte[] code=method.GetMethodBody().GetILAsByteArray();
        var ops=new Dictionary<ushort,OpCode>();
        foreach(var field in typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static)) {
            var op=(OpCode)field.GetValue(null); ops[unchecked((ushort)op.Value)]=op;
        }
        var calls=new List<string>();
        for(int p=0;p<code.Length;) {
            ushort value=code[p++]; if(value==0xfe) value=(ushort)(0xfe00|code[p++]);
            OpCode op=ops[value]; int n=0;
            switch(op.OperandType) {
                case OperandType.InlineNone: break;
                case OperandType.ShortInlineBrTarget: case OperandType.ShortInlineI: case OperandType.ShortInlineVar: n=1;break;
                case OperandType.InlineVar:n=2;break;
                case OperandType.InlineI8:case OperandType.InlineR:n=8;break;
                case OperandType.InlineSwitch:n=4+4*BitConverter.ToInt32(code,p);break;
                default:n=4;break;
            }
            if(op.OperandType==OperandType.InlineMethod) {
                var called=method.Module.ResolveMethod(BitConverter.ToInt32(code,p));
                if(called.DeclaringType.FullName=="System.GC") calls.Add(called.Name);
            }
            p+=n;
        }
        Console.WriteLine("DLL "+path+" MVID="+a.ManifestModule.ModuleVersionId+" FullCleanup_GC="+string.Join(",",calls.ToArray()));
    }
    static void AuditOwner(string repo, bool final, bool failDisconnect) {
        var tests=Assembly.LoadFrom(Path.Combine(repo,"PptCOM.Tests/bin/Release/PptCOM.Tests.exe"));
        var ppt=Assembly.LoadFrom(Path.Combine(repo,"PptCOM.Tests/bin/Release/PptCOM.dll"));
        var serverType=ppt.GetType("PptCOM.PptCOMServer",true);
        var fixture=tests.GetType("PptCOM.Tests.DispatchFixtureTests",true);
        var apiType=tests.GetType("PptCOM.Tests.DispatchFixtureTests+NativeApi",true);
        var scopeType=tests.GetType("PptCOM.Tests.DispatchFixtureTests+Scope",true);
        var profile=tests.GetType("PptCOM.Tests.DispatchFixtureTests+Profile",true);
        var node=tests.GetType("PptCOM.Tests.DispatchFixtureTests+Node",true);
        var api=apiType.GetConstructors(F)[0].Invoke(new object[]{Path.Combine(repo,"Build/pptcom-dispatch-fixture/x64/NativeDispatchFixture.dll")});
        var scope=scopeType.GetConstructors(F)[0].Invoke(new object[]{api,Enum.Parse(profile,"WithEvents"),"production final="+final});
        var server=Activator.CreateInstance(serverType);
        IntPtr slots=Marshal.AllocHGlobal(12);
        try {
            string[] names={"pptTotalPage","pptCurrentPage","offSignal"};
            for(int i=0;i<3;++i) {
                var setter=new DynamicMethod("SetAuditSlot",typeof(void),new[]{typeof(object),typeof(IntPtr)},serverType,true);
                var il=setter.GetILGenerator();il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Castclass,serverType);
                il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Conv_U);il.Emit(OpCodes.Stfld,serverType.GetField(names[i],F));il.Emit(OpCodes.Ret);
                ((Action<object,IntPtr>)setter.CreateDelegate(typeof(Action<object,IntPtr>)))(server,IntPtr.Add(slots,4*i));
                Marshal.WriteInt32(slots,4*i,0);
            }
            string[] fields={"Application","Presentation","Window"};
            string[] roots={"pptApplication","pptActivePresentation","pptSlideShowWindow"};
            object[] values=new object[3];
            for(int i=0;i<3;++i) {
                var field=scopeType.GetField(fields[i],F);values[i]=field.GetValue(scope);
                serverType.GetField(roots[i],F).SetValue(server,values[i]);field.SetValue(scope,null);
            }
            for(int i=0;i<100;++i) {
                serverType.GetMethod("TryGetActivePresentationFullNameForCompare",F).Invoke(server,new[]{values[0]});
                serverType.GetMethod("GetCurrentSlideIndex",F).Invoke(server,new[]{values[2]});
                serverType.GetMethod("GetTotalSlideIndex",F).Invoke(server,new[]{values[1]});
                serverType.GetMethod("GetSlideShowWindowsCount",F).Invoke(server,new[]{values[0]});
                serverType.GetMethod("ObserveBoundShow",F).Invoke(server,null);
            }
            var cpType=ppt.GetType("PptCOM.ComEventConnection",true);
            var connection=cpType.GetMethod("Connect",F).Invoke(null,new object[]{values[0],new Action<int>(k=>{})});
            serverType.GetField("eventConnection",F).SetValue(server,connection);
            if(failDisconnect) {
                var fault=(Delegate)apiType.GetField("SetEventFault",F).GetValue(api);
                var id=(uint)scopeType.GetField("Id",F).GetValue(scope);
                Check((int)fault.DynamicInvoke(id,2U,unchecked((int)0x8001010A),1U)==0,"SetEventFault failed");
            }
            serverType.GetMethod("FullCleanup",F).Invoke(server,new object[]{final});
            if(failDisconnect) {
                var registry=ppt.GetType("PptCOM.ComEventRetirementRegistry",true);
                int pending=(int)registry.GetProperty("PendingCount",F).GetValue(null,null);
                var held=scopeType.GetMethod("Stats",F).Invoke(scope,new[]{Enum.Parse(node,"Application")});
                long heldRefs=(long)held.GetType().GetField("References",F).GetValue(held);
                var events=scopeType.GetMethod("Events",F).Invoke(scope,null);
                long active=(long)events.GetType().GetField("ActiveConnections",F).GetValue(events);
                Console.WriteLine("after FullCleanup including GC: injected_Unadvise_busy pending="+pending+" application_refs="+heldRefs+" active_connections="+active);
                Check(pending==1 && heldRefs>1 && active==1,"Expected retained live CP not observed");
                var retryFault=(Delegate)apiType.GetField("SetEventFault",F).GetValue(api);
                var retryId=(uint)scopeType.GetField("Id",F).GetValue(scope);
                for(int retry=0;retry<3;++retry) {
                    Check((int)retryFault.DynamicInvoke(retryId,2U,unchecked((int)0x8001010A),1U)==0,"Set retry busy failed");
                    registry.GetMethod("Retry",F).Invoke(null,null);
                    Check((int)registry.GetProperty("PendingCount",F).GetValue(null,null)==1,"Busy CP unexpectedly dropped");
                }
                Console.WriteLine("after three explicitly injected busy retries: pending=1; provider remains referenced");
                // The last occurrence-only fault is consumed; the next attempt is healthy.
                registry.GetMethod("Retry",F).Invoke(null,null);
                Check((int)registry.GetProperty("PendingCount",F).GetValue(null,null)==0,"Explicit retry did not clear CP");
                Console.WriteLine("PASS explicit retry clears same CP acquisition after busy; GC did not clear it");
            }
            foreach(string name in new[]{"Application","Presentation","Window","View","Slides","Slide1","Slide2","Slide3","Navigation","ShowWindows"}) {
                var stat=scopeType.GetMethod("Stats",F).Invoke(scope,new[]{Enum.Parse(node,name)});
                long refs=(long)stat.GetType().GetField("References",F).GetValue(stat);
                Console.WriteLine("final="+final+" node="+name+" refs_before_fixture_owner_release="+refs);
                Check(refs==1,"Unbalanced native COM node: "+name);
            }
            ((IDisposable)scope).Dispose();
            Check((int)fixture.GetField("failures",F).GetValue(null)==0,"Native fixture failure");
            Console.WriteLine("PASS production FullCleanup, real event connection and 100 getter/observation cycles; final="+final);
        } finally { Marshal.FreeHGlobal(slots); }
    }
    static int Main(string[] args) {
        try {
            AppDomain.CurrentDomain.ReflectionOnlyAssemblyResolve+=(s,e)=>Assembly.ReflectionOnlyLoad(e.Name);
            if(args[0]=="--il") { ReportGc(Path.GetFullPath(args[1]));return 0; }
            string repo=Path.GetFullPath(args[0]);
            AuditOwner(repo,false,false);AuditOwner(repo,true,false);AuditOwner(repo,true,true);return 0;
        } catch(Exception ex) { Console.Error.WriteLine(ex);return 1; }
    }
}
