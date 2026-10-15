# 用户端只使用 PowerShell 2.0 / Win7 已有 API；现代开发机也走同一套解析规则。
if (-not ('Draw3PerfWire' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

public static class Draw3PerfWire {
    // 不依赖 PS3 JSON cmdlet；拒绝重复键、NaN、尾随文本和过深 JSON。
    sealed class Reader {
        readonly string s; int p;
        public Reader(string value) { s = value; }
        void Ws() { while(p < s.Length && Char.IsWhiteSpace(s[p])) ++p; }
        void Expect(char c) { Ws(); if(p >= s.Length || s[p++] != c) throw new FormatException("JSON token"); }
        string Str() {
            Expect('"'); StringBuilder b = new StringBuilder();
            while(p < s.Length) { char c = s[p++]; if(c == '"') return b.ToString();
                if(c < 32) throw new FormatException("JSON control");
                if(c != '\\') { b.Append(c); continue; }
                if(p >= s.Length) break; c = s[p++];
                switch(c) { case '"': case '\\': case '/': b.Append(c); break;
                case 'b': b.Append('\b'); break; case 'f': b.Append('\f'); break;
                case 'n': b.Append('\n'); break; case 'r': b.Append('\r'); break; case 't': b.Append('\t'); break;
                case 'u': if(p + 4 > s.Length) throw new FormatException("JSON unicode");
                    b.Append((char)Int32.Parse(s.Substring(p,4), NumberStyles.HexNumber, CultureInfo.InvariantCulture)); p += 4; break;
                default: throw new FormatException("JSON escape"); }
            } throw new FormatException("JSON string");
        }
        object Value(int depth) {
            if(depth > 32) throw new FormatException("JSON depth"); Ws();
            if(p >= s.Length) throw new FormatException("JSON missing"); char c = s[p];
            if(c == '"') return Str();
            if(c == '{') { ++p; Dictionary<string,object> d = new Dictionary<string,object>(StringComparer.Ordinal); Ws();
                if(p < s.Length && s[p] == '}') { ++p; return d; }
                while(true) { string key = Str(); Expect(':'); if(d.ContainsKey(key)) throw new FormatException("JSON duplicate key: " + key);
                    d.Add(key, Value(depth+1)); Ws(); if(p < s.Length && s[p] == '}') { ++p; return d; } Expect(','); }
            }
            if(c == '[') { ++p; ArrayList a = new ArrayList(); Ws();
                if(p < s.Length && s[p] == ']') { ++p; return a; }
                while(true) { a.Add(Value(depth+1)); Ws(); if(p < s.Length && s[p] == ']') { ++p; return a; } Expect(','); }
            }
            foreach(string token in new string[] { "true", "false", "null" }) {
                if(p + token.Length <= s.Length && String.CompareOrdinal(s,p,token,0,token.Length) == 0) {
                    p += token.Length; if(token == "null") return null; return token == "true";
                }
            }
            int first = p; if(s[p] == '-') ++p;
            if(p >= s.Length || s[p] < '0' || s[p] > '9') throw new FormatException("JSON number");
            if(s[p] == '0') ++p; else while(p < s.Length && s[p] >= '0' && s[p] <= '9') ++p;
            if(p < s.Length && s[p] == '.') { ++p; int start = p; while(p < s.Length && s[p] >= '0' && s[p] <= '9') ++p; if(p == start) throw new FormatException("JSON fraction"); }
            if(p < s.Length && (s[p] == 'e' || s[p] == 'E')) { ++p; if(p < s.Length && (s[p]=='+' || s[p]=='-')) ++p;
                int start = p; while(p < s.Length && s[p] >= '0' && s[p] <= '9') ++p; if(p == start) throw new FormatException("JSON exponent"); }
            string num = s.Substring(first,p-first); long integer;
            if(Int64.TryParse(num, NumberStyles.Integer, CultureInfo.InvariantCulture, out integer)) return integer;
            double real; if(!Double.TryParse(num, NumberStyles.Float, CultureInfo.InvariantCulture, out real) || Double.IsNaN(real) || Double.IsInfinity(real)) throw new FormatException("JSON finite"); return real;
        }
        public object Read() { object v = Value(0); Ws(); if(p != s.Length) throw new FormatException("JSON trailing text"); return v; }
    }
    public static object Parse(string value) { return new Reader(value).Read(); }
    static void Quote(StringBuilder b, string s) {
        b.Append('"'); foreach(char c in s) {
            switch(c) { case '"': b.Append("\\\""); break; case '\\': b.Append("\\\\"); break;
            case '\r': b.Append("\\r"); break; case '\n': b.Append("\\n"); break; case '\t': b.Append("\\t"); break;
            default: if(c < 32) b.Append("\\u" + ((int)c).ToString("x4")); else b.Append(c); break; }
        } b.Append('"');
    }
    static void Encode(StringBuilder b, object v, int depth) {
        if(depth > 32) throw new FormatException("JSON depth");
        if(v != null && v.GetType().FullName == "System.Management.Automation.PSObject")
            v = v.GetType().GetProperty("BaseObject").GetValue(v,null);
        if(v == null) { b.Append("null"); return; }
        if(v is string) { Quote(b,(string)v); return; }
        if(v is bool) { b.Append((bool)v ? "true" : "false"); return; }
        IDictionary dict = v as IDictionary; if(dict != null) { b.Append('{'); bool comma = false;
            foreach(DictionaryEntry e in dict) { if(comma) b.Append(','); comma = true; Quote(b,Convert.ToString(e.Key,CultureInfo.InvariantCulture)); b.Append(':'); Encode(b,e.Value,depth+1); } b.Append('}'); return; }
        IEnumerable list = v as IEnumerable; if(list != null) { b.Append('['); bool comma=false;
            foreach(object item in list) { if(comma) b.Append(','); comma=true; Encode(b,item,depth+1); } b.Append(']'); return; }
        if(v is IConvertible) { string n = Convert.ToString(v,CultureInfo.InvariantCulture); double test;
            if(Double.TryParse(n,NumberStyles.Float,CultureInfo.InvariantCulture,out test) && !Double.IsNaN(test) && !Double.IsInfinity(test)) { b.Append(n); return; } }
        throw new FormatException("JSON unsupported value type: " + v.GetType().FullName);
    }
    public static string Json(object v) { StringBuilder b = new StringBuilder(); Encode(b,v,0); return b.ToString(); }
    public static string[] Csv(string line) {
        List<string> result = new List<string>(); StringBuilder cell = new StringBuilder(); bool quoted=false, closed=false;
        for(int i=0; i<line.Length; ++i) { char c=line[i];
            if(quoted) { if(c=='"') { if(i+1 < line.Length && line[i+1]=='"') { cell.Append('"'); ++i; } else { quoted=false; closed=true; } } else cell.Append(c); }
            else if(c==',') { result.Add(cell.ToString()); cell.Length=0; closed=false; }
            else if(c=='"' && cell.Length==0 && !closed) quoted=true;
            else { if(closed || c=='"') throw new FormatException("CSV quoting"); cell.Append(c); }
        } if(quoted) throw new FormatException("CSV multiline/unterminated"); result.Add(cell.ToString()); return result.ToArray();
    }
    [StructLayout(LayoutKind.Sequential)] struct FileTime { public uint Low; public uint High; }
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(IntPtr h, out FileTime created, out FileTime exit, out FileTime kernel, out FileTime user);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool QueryFullProcessImageName(IntPtr h, int flags, StringBuilder path, ref int size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    static string[] IdentityAt(IntPtr h) {
        FileTime c,e,k,u; if(!GetProcessTimes(h,out c,out e,out k,out u)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        StringBuilder path=new StringBuilder(32768); int size=path.Capacity;
        if(!QueryFullProcessImageName(h,0,path,ref size)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        long tick=((long)c.High << 32) | c.Low; return new string[] { path.ToString(), tick.ToString(CultureInfo.InvariantCulture) };
    }
    public static string[] ProcessIdentity(int pid) {
        IntPtr h=OpenProcess(0x1000,false,pid); if(h==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        try { return IdentityAt(h); } finally { CloseHandle(h); }
    }
    // 最终 UIAccess/升权进程只申请读取和等待权限；同一持有句柄避免 PID 重用。
    public sealed class ProcessOwner : IDisposable {
        IntPtr handle;
        public readonly string ImagePath;
        public readonly string StartFiletime;
        public ProcessOwner(int pid) {
            handle=OpenProcess(0x1000|0x100000,false,pid);
            if(handle==IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            try { string[] identity=IdentityAt(handle); ImagePath=identity[0]; StartFiletime=identity[1]; }
            catch { Dispose(); throw; }
        }
        public bool WaitForExit(int milliseconds) {
            uint result=WaitForSingleObject(handle,(uint)milliseconds);
            if(result==0) return true; if(result==258) return false;
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
        public int ExitCode { get { uint code; if(!GetExitCodeProcess(handle,out code)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()); return unchecked((int)code); } }
        public void Dispose() { if(handle!=IntPtr.Zero) { CloseHandle(handle); handle=IntPtr.Zero; } }
    }
    public static ProcessOwner OpenProcessOwner(int pid) { return new ProcessOwner(pid); }
    // 两个后台 drain 始终读到 EOF；保留上限之后继续丢弃，不能让任一管道塞满。
    public sealed class BoundedWorker : IDisposable {
        sealed class Drain {
            readonly System.IO.Stream input; readonly string output; readonly long limit;
            public long Seen, Retained; public string Error;
            public readonly System.Threading.Thread Thread;
            public Drain(System.IO.Stream stream, string path, long bytes) {
                input=stream; output=path; limit=bytes;
                Thread=new System.Threading.Thread(Run); Thread.IsBackground=true; Thread.Start();
            }
            void Run() { try { using(System.IO.FileStream file=new System.IO.FileStream(output,System.IO.FileMode.CreateNew,System.IO.FileAccess.Write,System.IO.FileShare.Read)) {
                byte[] buffer=new byte[8192]; int count;
                while((count=input.Read(buffer,0,buffer.Length))>0) {
                    System.Threading.Interlocked.Add(ref Seen,count);
                    int keep=(int)Math.Min((long)count,Math.Max(0L,limit-Retained));
                    if(keep>0) { file.Write(buffer,0,keep); System.Threading.Interlocked.Add(ref Retained,keep); }
                }
            } } catch(Exception ex) { Error=ex.GetType().Name+": "+ex.Message; } }
        }
        readonly System.Diagnostics.Process process; readonly Drain stdout,stderr;
        public readonly int Pid; public readonly string ImagePath,StartFiletime;
        public BoundedWorker(string exe,string arguments,string working,string outPath,string errPath,long limit) {
            System.Diagnostics.ProcessStartInfo info=new System.Diagnostics.ProcessStartInfo(exe,arguments);
            info.WorkingDirectory=working; info.UseShellExecute=false; info.CreateNoWindow=true;
            info.RedirectStandardOutput=true; info.RedirectStandardError=true;
            process=new System.Diagnostics.Process(); process.StartInfo=info;
            try { if(!process.Start()) throw new InvalidOperationException("worker not started");
                IntPtr handle=process.Handle; Pid=process.Id; string[] id=IdentityAt(handle); ImagePath=id[0]; StartFiletime=id[1];
                stdout=new Drain(process.StandardOutput.BaseStream,outPath,limit);
                stderr=new Drain(process.StandardError.BaseStream,errPath,limit);
            } catch { try { if(!process.HasExited) process.Kill(); } catch {} process.Dispose(); throw; }
        }
        public bool WaitForExit(int milliseconds) { return process.WaitForExit(milliseconds); }
        public bool HasExited { get { return process.HasExited; } }
        public int ExitCode { get { return process.ExitCode; } }
        public bool Terminate(int milliseconds) { if(!process.HasExited) process.Kill(); return process.WaitForExit(milliseconds); }
        public bool FinishDrains(int milliseconds) { bool a=stdout.Thread.Join(milliseconds); bool b=stderr.Thread.Join(milliseconds); return a && b; }
        public long StdoutSeen { get { return System.Threading.Interlocked.Read(ref stdout.Seen); } }
        public long StderrSeen { get { return System.Threading.Interlocked.Read(ref stderr.Seen); } }
        public long StdoutRetained { get { return System.Threading.Interlocked.Read(ref stdout.Retained); } }
        public long StderrRetained { get { return System.Threading.Interlocked.Read(ref stderr.Retained); } }
        public string StdoutError { get { return stdout.Error; } }
        public string StderrError { get { return stderr.Error; } }
        public void Dispose() { process.Dispose(); }
    }
    public static BoundedWorker StartWorker(string exe,string arguments,string working,string stdout,string stderr,long limit) {
        return new BoundedWorker(exe,arguments,working,stdout,stderr,limit);
    }
    // CommandLineToArgvW 的反斜杠/引号规则；不通过 cmd.exe 拼接执行。
    public static string QuoteArgument(string value) {
        StringBuilder b=new StringBuilder(); b.Append('"'); int slashes=0;
        foreach(char c in value) { if(c=='\\') { ++slashes; continue; }
            if(c=='"') { b.Append('\\',slashes*2+1); b.Append('"'); }
            else { b.Append('\\',slashes); b.Append(c); } slashes=0;
        } b.Append('\\',slashes*2); b.Append('"'); return b.ToString();
    }

}
'@
}

function Read-PerfJson([string]$Path) {
    $info = New-Object IO.FileInfo($Path)
    if (-not $info.Exists) { throw "缺少 JSON: $Path" }
    if ($info.Length -gt 4194304) { throw "JSON 超过 4 MiB: $Path" }
    $value=[Draw3PerfWire]::Parse([IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8))
    if ($value -isnot [Collections.IDictionary]) { throw "JSON 根必须是对象: $Path" }
    return $value
}

function Get-PerfJsonInteger($Record,[string]$Field,[long]$Minimum=0,[long]$Maximum=[long]::MaxValue) {
    $value=$Record[$Field]
    # 本解析器对 JSON 整数字面量只返回 Int64，禁止 PS 把 true/1.4/"1" 强转成版本或 PID。
    if ($value -isnot [long] -or $value -lt $Minimum -or $value -gt $Maximum) { throw "JSON 整数类型/范围非法: $Field" }
    return $value
}

function Get-PerfJsonString($Record,[string]$Field) {
    $value=$Record[$Field]
    if ($value -isnot [string] -or $value.Length -eq 0) { throw "JSON 字符串类型/值非法: $Field" }
    return $value
}

function Write-PerfJson([string]$Path, $Value) {
    $temporary = $Path + '.writing-' + [Guid]::NewGuid().ToString('N')
    $encoding = New-Object Text.UTF8Encoding($true)
    [IO.File]::WriteAllText($temporary, [Draw3PerfWire]::Json($Value) + [Environment]::NewLine, $encoding)
    if ([IO.File]::Exists($Path)) { [IO.File]::Delete($Path) }
    [IO.File]::Move($temporary, $Path)
}

function Get-PerfSha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}

function Test-PerfIdentity($Record, $Launch, [string]$Root, [bool]$AllowRelocated=$false) {
    [void](Get-PerfJsonInteger $Record 'schema_version' 1 1)
    [void](Get-PerfJsonInteger $Record 'final_pid' 1 2147483647)
    foreach ($field in @('session_id','process_start_time','process_start_filetime_utc','exe_path','output_directory','status')) { [void](Get-PerfJsonString $Record $field) }
    $expectedLeaf=[IO.Path]::GetFileName($Root)
    if ($AllowRelocated) {
        if (-not [IO.Path]::IsPathRooted([string]$Record['output_directory'])) { throw '原始握手目录不是绝对路径' }
        $expectedLeaf=[IO.Path]::GetFileName([IO.Path]::GetFullPath([string]$Record['output_directory']).TrimEnd('\'))
    }
    if ([string]$Record['session_id'] -cne $expectedLeaf) { throw '握手 session_id 不符' }
    $identityPid=0; $identityStamp=0L
    if (-not [int]::TryParse([string]$Record['final_pid'],[ref]$identityPid) -or $identityPid -le 0 -or -not [long]::TryParse([string]$Record['process_start_filetime_utc'],[ref]$identityStamp) -or $identityStamp -le 0) { throw '握手 PID/创建时间非法' }
    $identityTime=[DateTime]::Parse([string]$Record['process_start_time'],[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind)
    if ($identityTime.Kind -ne [DateTimeKind]::Utc -or [Math]::Abs($identityTime.ToFileTimeUtc()-$identityStamp) -gt 10000) { throw '握手 UTC/FILETIME 不符' }
    if (-not [string]$Record['exe_path'] -or -not [IO.Path]::IsPathRooted([string]$Record['exe_path'])) { throw '握手缺少绝对 EXE 路径' }
    if (-not $AllowRelocated -and [IO.Path]::GetFullPath([string]$Record['output_directory']).TrimEnd('\') -ne $Root.TrimEnd('\')) { throw '握手 output_directory 不符' }
    if ($Launch) {
        foreach ($field in @('session_id', 'final_pid', 'process_start_time', 'process_start_filetime_utc', 'exe_path', 'output_directory')) {
            if ([string]$Record[$field] -cne [string]$Launch[$field]) { throw "完成身份不符: $field" }
        }
    }
}

function Wait-PerfFinalProcess([string]$Root, [string]$ExePath, [int]$LaunchTimeoutSeconds, [int]$CaptureTimeoutSeconds, [int]$PollMilliseconds = 1000) {
    $launchPath = Join-Path $Root 'launch.json'
    $completionPath = Join-Path $Root 'completion.json'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $launch = $null; $final = $null
    while ($clock.Elapsed.TotalSeconds -lt $LaunchTimeoutSeconds) {
        if ([IO.File]::Exists($launchPath)) {
            try {
                $launch = Read-PerfJson $launchPath
                Test-PerfIdentity $launch $null $Root
                if ($launch['status'] -ne 'started') { throw 'launch 状态不是 started' }
                if ([IO.Path]::GetFullPath([string]$launch['output_directory']).TrimEnd('\') -ne $Root.TrimEnd('\')) { throw 'launch 目录不符' }
                if (-not [string]$launch['exe_path'] -or [IO.Path]::GetFullPath([string]$launch['exe_path']) -ne $ExePath) { throw 'launch EXE 不符' }
                $finalPidValue = 0
                if (-not [int]::TryParse([string]$launch['final_pid'], [ref]$finalPidValue) -or $finalPidValue -le 0) { throw 'launch PID 无效' }
                $stamp = 0L
                if (-not [long]::TryParse([string]$launch['process_start_filetime_utc'], [ref]$stamp) -or $stamp -le 0) { throw 'launch 创建时间无效' }
                $iso = [DateTime]::Parse([string]$launch['process_start_time'], [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
                if ($iso.Kind -ne [DateTimeKind]::Utc -or [Math]::Abs($iso.ToFileTimeUtc() - $stamp) -gt 10000) { throw 'launch ISO/FILETIME 时间不符' }
                $collectorSessionPath = Join-Path $Root 'collector-session.json'
                if ([IO.File]::Exists($collectorSessionPath)) {
                    $collectorSession = Read-PerfJson $collectorSessionPath
                    $captureStarted = [DateTime]::Parse([string]$collectorSession['started_utc'], [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
                    if ($iso -lt $captureStarted.AddMilliseconds(-1)) { throw '握手来自本次会话创建之前的进程' }
                }
                try { $final = [Draw3PerfWire]::OpenProcessOwner($finalPidValue) }
                catch {
                    $openFailure=$_.Exception
                    while ($openFailure.InnerException) { $openFailure=$openFailure.InnerException }
                    if ($openFailure -isnot [ComponentModel.Win32Exception] -or @([int]87,[int]1168) -notcontains $openFailure.NativeErrorCode) { throw ('最终进程读取/等待句柄失败（可能权限不足）: '+$openFailure.Message) }
                    # 快速退出也必须有同身份完成文件；不能拿已重用的 PID 当作原进程。
                    if (-not [IO.File]::Exists($completionPath)) { throw '最终进程已消失且没有 completion' }
                    $final = $null
                }
                if ($final) {
                    if ([IO.Path]::GetFullPath($final.ImagePath) -ne $ExePath -or $final.StartFiletime -ne [string]$stamp) { throw '最终 PID 创建时间或 EXE 不匹配' }
                }
                break
            } catch { if ($final) { $final.Dispose() }; return @{ status='incomplete'; reason=('launch_identity: ' + $_.Exception.Message); launch=$launch; completion=$null } }
        }
        $failures = @(Get-ChildItem -LiteralPath $Root -Filter 'launch-failure-*.json' -ErrorAction SilentlyContinue)
        foreach ($failure in $failures) {
            try { $item = Read-PerfJson $failure.FullName
                if ($item['session_id'] -eq [IO.Path]::GetFileName($Root) -and $item['status'] -eq 'failed_before_final') { return @{status='failed'; reason=[string]$item['reason']; launch=$null; completion=$null} }
            } catch { }
        }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    if (-not $launch) { return @{status='incomplete'; reason='final_handshake_timeout; 软件可能稍后启动，采集器没有终止软件'; launch=$null; completion=$null} }
    $clock.Reset(); $clock.Start()
    try {
        while ($final -and -not $final.WaitForExit($PollMilliseconds)) {
            if ($CaptureTimeoutSeconds -gt 0 -and $clock.Elapsed.TotalSeconds -ge $CaptureTimeoutSeconds) { return @{status='incomplete'; reason='capture_wait_timeout; 软件仍在运行，采集器没有终止软件'; launch=$launch; completion=$null} }
        }
        if (-not [IO.File]::Exists($completionPath)) { return @{status='incomplete'; reason='final_exited_without_completion'; launch=$launch; completion=$null} }
        $completion = Read-PerfJson $completionPath
        Test-PerfIdentity $completion $launch $Root
        if (@('completed','failed','incomplete') -cnotcontains [string]$completion['status']) { throw 'completion 状态无效' }
        $completionExit=Get-PerfJsonInteger $completion 'exit_code' -2147483648 2147483647
        if ($final -and $completionExit -ne $final.ExitCode) { throw 'completion / 进程退出码不符' }
        return @{status=[string]$completion['status']; reason=[string]$completion['reason']; launch=$launch; completion=$completion; product_stopped_confirmed=($null -ne $final); product_exit_code=$completionExit}
    } catch { return @{status='incomplete'; reason=('completion_identity: ' + $_.Exception.Message); launch=$launch; completion=$null} }
    finally { if ($final) { $final.Dispose() } }
}

function Get-PerfExeArchitecture([string]$Path) {
    $stream=[IO.File]::OpenRead($Path); $reader=New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw '不是 PE 文件' }
        $stream.Position=0x3c; $offset=$reader.ReadUInt32(); if ($offset -gt $stream.Length-6) { throw 'PE 偏移无效' }
        $stream.Position=$offset; if ($reader.ReadUInt32() -ne 0x4550) { throw 'PE 签名无效' }
        switch ($reader.ReadUInt16()) { 0x14c {return 'x86'} 0x8664 {return 'x64'} 0xaa64 {return 'arm64'} default {throw '未知 PE 架构'} }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Test-PerfV2Capability([string]$Path) {
    # sentinel 是已引用的正式能力合同，旧版只有性能参数字面量也必须拒绝。
    $bytes=[IO.File]::ReadAllBytes($Path)
    $marker='INKEYS_DRAW3_DIAGNOSTICS_V2'
    if ([Text.Encoding]::ASCII.GetString($bytes).IndexOf($marker,[StringComparison]::Ordinal) -lt 0 -and [Text.Encoding]::Unicode.GetString($bytes).IndexOf($marker,[StringComparison]::Ordinal) -lt 0) { throw 'EXE 缺少诊断包 2 能力标记；拒绝向旧 EXE 发送新参数' }
}

function Test-PerfPackageManifest($Manifest,[string]$Executable,[string]$ScriptDirectory) {
    [void](Get-PerfJsonInteger $Manifest 'schema_version' 1 1)
    [void](Get-PerfJsonInteger $Manifest 'capture_schema' 2 2)
    [void](Get-PerfJsonInteger $Manifest 'package_revision' 2 2)
    [void](Get-PerfJsonInteger $Manifest 'diagnostic_package_version' 2 2)
    [void](Get-PerfJsonInteger $Manifest 'resource_probe_schema' 1 1)
    [void](Get-PerfJsonInteger $Manifest 'probe_revision' 2 2)
    if ((Get-PerfJsonString $Manifest 'capability_marker') -cne 'INKEYS_DRAW3_DIAGNOSTICS_V2' -or (Get-PerfJsonString $Manifest 'command_line') -cne '--draw3-performance' -or (Get-PerfJsonString $Manifest 'resource_probe_command') -cne '--draw3-resource-probe' -or (Get-PerfJsonString $Manifest 'resource_probe_list_command') -cne '--draw3-resource-probe-list') { throw '包 manifest 的正式能力/入口不支持' }
    if ((Get-PerfJsonString $Manifest 'exe_sha256') -cne (Get-PerfSha256 $Executable)) { throw 'EXE SHA256 与包 manifest 不符；请使用配套诊断包' }
    if ((Get-PerfJsonString $Manifest 'exe_architecture') -cne (Get-PerfExeArchitecture $Executable)) { throw 'EXE PE 架构与 manifest 不符' }
    Test-PerfV2Capability $Executable
    $hashes=$Manifest['script_sha256']; if ($hashes -isnot [Collections.IDictionary] -or $hashes.Count -ne 5) { throw '用户文件 SHA256 清单不完整' }
    foreach ($name in @('Run-Draw3-Performance.cmd','Run-Draw3-Performance.ps1','Analyze-Draw3-Performance.ps1','Performance.Common.ps1','README.md')) {
        if ((Get-PerfJsonString $hashes $name) -cne (Get-PerfSha256 (Join-Path $ScriptDirectory $name))) { throw ('用户文件 SHA256 不符: '+$name) }
    }
    $expected=@{'session.json'=2;'initialization.json'=1;'frames.csv'=2;'input-events.csv'=1;'gpu.csv'=2;'resources.csv'=1;'graphics-calls.csv'=1;'slow-graphics-calls.csv'=1;'graphics-groups.csv'=1}
    if ($Manifest['raw_schemas'] -isnot [Collections.IDictionary]) { throw '缺少 raw_schemas' }
    foreach ($name in $expected.Keys) { [void](Get-PerfJsonInteger $Manifest['raw_schemas'] $name $expected[$name] $expected[$name]) }
}

function Join-PerfArguments($Arguments) {
    $quoted=@(); foreach ($value in $Arguments) { $quoted += [Draw3PerfWire]::QuoteArgument([string]$value) }; return ($quoted -join ' ')
}

function Get-PerfDirectoryBytes([string]$Directory) {
    $sum=0L; foreach ($file in @(Get-ChildItem -LiteralPath $Directory -Recurse -ErrorAction Stop)) { if (-not $file.PSIsContainer) { $sum+=$file.Length } }; return $sum
}

function Invoke-PerfOwnedWorker([string]$Executable,$Arguments,[string]$Directory,[int]$TimeoutMilliseconds,[string]$BudgetDirectory=$Directory,[long]$ByteBudget=67108864) {
    $worker=$null; $clock=[Diagnostics.Stopwatch]::StartNew()
    $record=@{schema_version=1;status='not_started';started_utc=[DateTime]::UtcNow.ToString('o');exit_code=$null;timed_out=$false;termination_requested=$false;stopped_confirmed=$false;drains_complete=$false;reason=$null;pid=$null;exe_path=$null;process_start_filetime_utc=$null;stdout_seen_bytes=0L;stderr_seen_bytes=0L;stdout_retained_bytes=0L;stderr_retained_bytes=0L}
    try {
        $worker=[Draw3PerfWire]::StartWorker($Executable,(Join-PerfArguments $Arguments),(Split-Path -Parent $Executable),(Join-Path $Directory 'stdout.txt'),(Join-Path $Directory 'stderr.txt'),1048576)
        $record.pid=$worker.Pid; $record.exe_path=$worker.ImagePath; $record.process_start_filetime_utc=$worker.StartFiletime; $record.status='running'
        while (-not $worker.WaitForExit(100)) {
            if ($clock.Elapsed.TotalMilliseconds -ge $TimeoutMilliseconds) { $record.status='timeout'; $record.timed_out=$true; $record.reason='outer_deadline'; break }
            if ((Get-PerfDirectoryBytes $BudgetDirectory) -gt $ByteBudget) { $record.status='limit'; $record.reason='matrix_disk_budget'; break }
        }
        if (-not $worker.HasExited) { $record.termination_requested=$true; $record.stopped_confirmed=$worker.Terminate(3000) }
        else { $record.stopped_confirmed=$true }
        if ($record.stopped_confirmed) {
            $record.exit_code=$worker.ExitCode; $record.drains_complete=$worker.FinishDrains(1000)
            if ($record.status -eq 'running') { $record.status='exited' }
        } else { $record.status='termination_unconfirmed'; $record.reason='owned_worker_not_stopped' }
    } catch {
        $record.reason=$_.Exception.Message; $record.status='worker_error'
        if ($worker) { try { $record.termination_requested=$true; $record.stopped_confirmed=$worker.Terminate(3000) } catch { $record.reason+='; cleanup: '+$_.Exception.Message } }
    } finally {
        if ($worker) {
            $record.stdout_seen_bytes=$worker.StdoutSeen; $record.stderr_seen_bytes=$worker.StderrSeen; $record.stdout_retained_bytes=$worker.StdoutRetained; $record.stderr_retained_bytes=$worker.StderrRetained
            $record.stdout_discarded_bytes=$record.stdout_seen_bytes-$record.stdout_retained_bytes;$record.stderr_discarded_bytes=$record.stderr_seen_bytes-$record.stderr_retained_bytes
            $record.stdout_error=$worker.StdoutError; $record.stderr_error=$worker.StderrError; $worker.Dispose()
        }
        $record.elapsed_ms=$clock.Elapsed.TotalMilliseconds; $record.ended_utc=[DateTime]::UtcNow.ToString('o')
        Write-PerfJson (Join-Path $Directory 'worker-status.json') $record
    }
    return $record
}

function Test-PerfProbeIdentity($Record,$List,$Case,[string]$SessionId,[string]$CaseDirectory,$Observed) {
    [void](Get-PerfJsonInteger $Record 'schema_version' 1 1); [void](Get-PerfJsonInteger $Record 'probe_revision' 2 2)
    foreach ($name in @('capability_marker','profile','comparison_path')) { if ((Get-PerfJsonString $Record $name) -cne [string]$List[$name]) { throw ('probe 合同不符: '+$name) } }
    foreach ($name in @('case_id','profile_id','requested_backend')) { if ((Get-PerfJsonString $Record $name) -cne [string]$Case[$name]) { throw ('probe case 身份不符: '+$name) } }
    if ((Get-PerfJsonString $Record 'session_id') -cne $SessionId -or (Get-PerfJsonInteger $Record 'final_pid' 1 2147483647) -ne $Observed['pid']) { throw 'probe session/PID 不符' }
    $stamp=Get-PerfJsonString $Record 'process_start_filetime_utc'; $parsed=0L
    if (-not [long]::TryParse($stamp,[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$parsed) -or $parsed -le 0 -or $stamp -cne [string]$Observed['process_start_filetime_utc']) { throw 'probe FILETIME 不符' }
    foreach ($name in @('exe_path','output_directory')) { $value=Get-PerfJsonString $Record $name; if (-not [IO.Path]::IsPathRooted($value)) { throw ('probe 不是绝对路径: '+$name) } }
    if ([IO.Path]::GetFullPath([string]$Record['exe_path']) -ne [IO.Path]::GetFullPath([string]$Observed['exe_path']) -or [IO.Path]::GetFullPath([string]$Record['output_directory']).TrimEnd('\') -ne $CaseDirectory.TrimEnd('\')) { throw 'probe EXE/目录不符' }
}

function Test-PerfProbeResult($Result,$Start,$List,$Case,[string]$SessionId,[string]$CaseDirectory,$Observed,[string]$RawDirectory='') {
    if (-not $RawDirectory) {$RawDirectory=$CaseDirectory}
    Test-PerfProbeIdentity $Start $List $Case $SessionId $CaseDirectory $Observed
    Test-PerfProbeIdentity $Result $List $Case $SessionId $CaseDirectory $Observed
    foreach ($name in @('session_id','case_id','profile_id','requested_backend','final_pid','process_start_filetime_utc','exe_path','output_directory')) { if ([string]$Result[$name] -cne [string]$Start[$name]) { throw ('probe started/result 身份变化: '+$name) } }
    $exit=Get-PerfJsonInteger $Result 'exit_code' -2147483648 2147483647
    $expectedCodes=@{complete=0;unsupported=3;failed=4;partial=5}; $status=Get-PerfJsonString $Result 'status'
    if (-not $expectedCodes.ContainsKey($status) -or $exit -ne $expectedCodes[$status] -or $exit -ne $Observed['exit_code'] -or $Observed['status'] -ne 'exited' -or $Observed['stopped_confirmed'] -ne $true -or $Observed['drains_complete'] -ne $true) { throw 'probe 状态/实际退出码/停止证据不符' }
    $definition=$Result['case_definition']; if ($definition -isnot [Collections.IDictionary]) { throw 'probe 缺少 case_definition' }
    foreach ($name in @('case_id','profile_id','requested_backend','update_method','constant_write_method')) { if ((Get-PerfJsonString $definition $name) -cne [string]$Case[$name]) { throw ('probe 工作量不符: '+$name) } }
    foreach ($name in @('resource_bytes','write_bytes','draws_per_iteration','width','height')) { if ((Get-PerfJsonInteger $definition $name) -ne (Get-PerfJsonInteger $Case $name)) { throw ('probe 工作量不符: '+$name) } }
    foreach ($name in @('calls_seen','calls_retained','calls_dropped','invalid_intervals','cold_iterations_completed','warmup_iterations_completed','measured_iterations_completed','qpc_frequency','start_qpc','end_qpc')) { [void](Get-PerfJsonInteger $Result $name) }
    if ($Result['calls_seen'] -ne ($Result['calls_retained']+$Result['calls_dropped']) -or $Result['calls_retained'] -gt 4096) { throw 'probe 调用计数不符' }
    if ($Result['drain_complete'] -isnot [bool]) { throw 'probe drain_complete 类型非法' }
    if ($status -eq 'complete') {
        if ((Get-PerfJsonString $Result 'actual_backend') -cne [string]$Case['requested_backend'] -or $Result['qpc_frequency'] -le 0 -or $Result['start_qpc'] -le 0 -or $Result['end_qpc'] -lt $Result['start_qpc'] -or $Result['measured_iterations_completed'] -ne $List['measured_iterations'] -or $Result['warmup_iterations_completed'] -ne $List['warmup_iterations'] -or $Result['cold_iterations_completed'] -ne $List['cold_iterations'] -or $Result['calls_dropped'] -ne 0 -or $Result['invalid_intervals'] -ne 0) { throw 'probe complete 缺少同backend/工作量/有效时间/完整计量' }
    }
    foreach ($field in @('thread_cpu_ms','process_cpu_ms')) {
        $value=$Result[$field]
        if ($null -ne $value -and (($value -isnot [long] -and $value -isnot [double]) -or [double]::IsNaN([double]$value) -or [double]::IsInfinity([double]$value) -or $value -lt 0)) {throw ('probe CPU字段类型/范围非法: '+$field)}
    }
    if ($status -eq 'complete' -and $Case['draws_per_iteration'] -gt 0) {
        [void](Get-PerfJsonInteger $Result['shader'] 'vertex_resource_id' 302 302);[void](Get-PerfJsonInteger $Result['shader'] 'pixel_resource_id' 301 301)
        foreach ($field in @('vertex_bytes','pixel_bytes')) {[void](Get-PerfJsonInteger $Result['shader'] $field 1 4194304)}
        foreach ($field in @('vertex_fnv1a64','pixel_fnv1a64')) {$hashText=Get-PerfJsonString $Result['shader'] $field;$hashValue=[System.UInt64]0;if (-not [System.UInt64]::TryParse($hashText,[Globalization.NumberStyles]::None,[Globalization.CultureInfo]::InvariantCulture,[ref]$hashValue)) {throw 'probe shader身份非法'}}
    }
    foreach ($name in @('calls.csv','resources.csv','gpu.csv')) { $file=New-Object IO.FileInfo((Join-Path $RawDirectory $name)); if (-not $file.Exists -or $file.Length -gt 4194304) { throw ('probe raw 缺失或超过4MiB: '+$name) } }
    return $status
}

function Invoke-PerfResourceMatrix([string]$Root,[string]$Executable,[string]$SessionId,[string]$ExeHash,[bool]$ProductStopped,[bool]$NormalShutdown,[bool]$Skip=$false,[int]$CaseTimeoutSeconds=20,[int]$MatrixTimeoutSeconds=480,$PrefixArguments=@(),$ProductNames=@()) {
    if ($CaseTimeoutSeconds -lt 1 -or $CaseTimeoutSeconds -gt 20 -or $MatrixTimeoutSeconds -lt 1 -or $MatrixTimeoutSeconds -gt 480) { throw '资源矩阵超时超出固定上限' }
    $matrixRoot=Join-Path $Root 'resource-probes'; if ([IO.Directory]::Exists($matrixRoot)) { throw '资源矩阵目录已存在，不覆盖或重跑初次对照' }
    [void][IO.Directory]::CreateDirectory($matrixRoot); $path=Join-Path $matrixRoot 'matrix.json'
    $matrix=@{schema_version=1;probe_revision=2;status='running';reason=$null;session_id=$SessionId;exe_path=$Executable;exe_sha256=$ExeHash;started_utc=[DateTime]::UtcNow.ToString('o');profile='draw3-resource-v2-fixed';case_timeout_seconds=$CaseTimeoutSeconds;matrix_timeout_seconds=$MatrixTimeoutSeconds;stdout_byte_limit=1048576;stderr_byte_limit=1048576;cleanup_reserve_seconds=6;raw_file_byte_limit=4194304;disk_byte_budget=67108864;product_stopped_confirmed=$ProductStopped;normal_shutdown=$NormalShutdown;expected_cases=@();cases=(New-Object Collections.ArrayList)}
    if ($Skip) { $matrix.status='skipped'; $matrix.reason='explicit_skip_resource_probes' }
    elseif (-not $ProductStopped -or -not $NormalShutdown) { $matrix.status='skipped'; $matrix.reason='skipped_product_not_confirmed_stopped' }
    foreach ($name in $ProductNames) { if ([Diagnostics.Process]::GetProcessesByName([string]$name).Length -gt 0) { $matrix.status='skipped'; $matrix.reason='skipped_product_process_present' } }
    if ($matrix.status -eq 'skipped') { $matrix.ended_utc=[DateTime]::UtcNow.ToString('o'); Write-PerfJson $path $matrix; return $matrix }
    $clock=[Diagnostics.Stopwatch]::StartNew(); Write-PerfJson $path $matrix; $lockedExe=$null
    try {
        # 采集结束后重新验同字节并禁止写/删，防自动更新把新参数交给不同版本EXE。
        $lockedExe=New-Object IO.FileStream($Executable,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        if ((Get-PerfSha256 $Executable) -cne $ExeHash) {throw 'EXE在产品运行后已改变；不发送probe参数'}
        $listDirectory=Join-Path $matrixRoot '_list'; [void][IO.Directory]::CreateDirectory($listDirectory)
        $observed=Invoke-PerfOwnedWorker $Executable (@($PrefixArguments)+@('--draw3-resource-probe-list')) $listDirectory ([Math]::Max(1,[Math]::Min(10000,$MatrixTimeoutSeconds*1000-6000))) $matrixRoot
        $matrix['list_worker']=$observed
        if ($observed.status -ne 'exited' -or $observed.exit_code -ne 0 -or $observed.stopped_confirmed -ne $true -or $observed.drains_complete -ne $true -or $observed.stdout_seen_bytes -gt 1048576) { throw 'probe-list 未有效完成；不发送任何 case 参数' }
        $list=Read-PerfJson (Join-Path $listDirectory 'stdout.txt')
        [void](Get-PerfJsonInteger $list 'schema_version' 1 1); [void](Get-PerfJsonInteger $list 'probe_revision' 2 2)
        if ((Get-PerfJsonString $list 'capability_marker') -cne 'INKEYS_DRAW3_DIAGNOSTICS_V2' -or (Get-PerfJsonString $list 'profile') -cne 'draw3-resource-v2-fixed' -or (Get-PerfJsonString $list 'comparison_path') -cne 'offscreen_embedded_301_302_contract') { throw 'probe-list 能力/profile 不支持' }
        [void](Get-PerfJsonInteger $list 'native_budget_ms' 1 8000); [void](Get-PerfJsonInteger $list 'call_capacity' 1 4096)
        foreach ($name in @('cold_iterations','warmup_iterations')) { [void](Get-PerfJsonInteger $list $name 1 1) }; [void](Get-PerfJsonInteger $list 'measured_iterations' 4 4)
        $count=Get-PerfJsonInteger $list 'case_count' 1 64
        if ($list['cases'] -isnot [Collections.IList] -or $list['cases'].Count -ne $count) { throw 'probe-list case_count 不符' }
        $ids=@{}
        foreach ($case in $list['cases']) {
            $id=Get-PerfJsonString $case 'case_id'; $profile=Get-PerfJsonString $case 'profile_id'; $backend=Get-PerfJsonString $case 'requested_backend'
            if ($id -cnotmatch '^[a-z0-9][a-z0-9_-]{0,95}$' -or $profile -cnotmatch '^[a-z0-9][a-z0-9_-]{0,63}$' -or @('hardware','warp') -cnotcontains $backend -or $id -cne ($backend+'-'+$profile) -or $ids.ContainsKey($id)) { throw 'probe-list case 身份非法或重复' }
            foreach ($name in @('resource_bytes','write_bytes','draws_per_iteration','width','height')) { [void](Get-PerfJsonInteger $case $name) }
            foreach ($name in @('update_method','constant_write_method')) { [void](Get-PerfJsonString $case $name) }
            $ids[$id]=$true
        }
        $matrix.expected_cases=$list['cases']; $matrix['list']=$list; Write-PerfJson $path $matrix; $stopReason=$null
        $ordinal=0
        foreach ($case in $list['cases']) {
            $ordinal++;$caseLeaf='c'+$ordinal.ToString('D3')
            $id=[string]$case['case_id']; $remaining=[int][Math]::Floor($MatrixTimeoutSeconds*1000-$clock.Elapsed.TotalMilliseconds-6000)
            if ($remaining -le 0) { $stopReason='matrix_deadline' }
            if ($stopReason) { [void]$matrix.cases.Add(@{case_id=$id;directory=$caseLeaf;status='skipped';reason=$stopReason;identity_verified=$false}); continue }
            if ((Get-PerfDirectoryBytes $matrixRoot) -gt 67108864) { $stopReason='matrix_disk_budget'; [void]$matrix.cases.Add(@{case_id=$id;directory=$caseLeaf;status='skipped';reason=$stopReason;identity_verified=$false}); continue }
            $caseDirectory=Join-Path $matrixRoot $caseLeaf
            if ([IO.Directory]::Exists($caseDirectory)) { throw 'case 目录已存在，拒绝复用' }; [void][IO.Directory]::CreateDirectory($caseDirectory)
            Write-PerfJson (Join-Path $caseDirectory 'request.json') @{schema_version=1;probe_revision=2;session_id=$SessionId;case_id=$id;exe_path=$Executable;exe_sha256=$ExeHash;output_directory=$caseDirectory;case_definition=$case;outer_timeout_ms=[Math]::Min($CaseTimeoutSeconds*1000,$remaining);created_utc=[DateTime]::UtcNow.ToString('o');identity_policy='new_case_directory_and_held_PID_FILETIME_EXE'}
            $observed=Invoke-PerfOwnedWorker $Executable (@($PrefixArguments)+@('--draw3-resource-probe',$id,$caseDirectory,$SessionId)) $caseDirectory ([Math]::Min($CaseTimeoutSeconds*1000,$remaining)) $matrixRoot
            $entry=@{case_id=$id;directory=$caseLeaf;profile_id=$case['profile_id'];requested_backend=$case['requested_backend'];status=$observed.status;reason=$observed.reason;identity_verified=$false;worker=$observed}
            try {
                $start=Read-PerfJson (Join-Path $caseDirectory 'probe-start.json'); $result=Read-PerfJson (Join-Path $caseDirectory 'probe-result.json')
                $entry.status=Test-PerfProbeResult $result $start $list $case $SessionId $caseDirectory $observed $caseDirectory
                $entry.identity_verified=$true; $entry.reason=$result['reason']
            } catch { $entry.reason=$_.Exception.Message; if ($observed.status -eq 'exited') { $entry.status='invalid_result' } }
            [void]$matrix.cases.Add($entry); Write-PerfJson $path $matrix
            if ($observed.stopped_confirmed -ne $true) { $stopReason='owned_worker_stop_unconfirmed' }
        }
        $matrix.status='complete'; foreach ($entry in $matrix.cases) { if ($entry.status -ne 'complete' -or $entry.identity_verified -ne $true) { $matrix.status='partial' } }
        $matrix.reason=$stopReason
    } catch { $matrix.status='partial'; $matrix.reason=$_.Exception.Message }
    finally {if ($lockedExe) {$lockedExe.Dispose()}}
    $matrix.elapsed_ms=$clock.Elapsed.TotalMilliseconds; $matrix.ended_utc=[DateTime]::UtcNow.ToString('o'); Write-PerfJson $path $matrix; return $matrix
}
