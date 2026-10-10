using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;

[assembly: System.Runtime.Versioning.TargetFramework(".NETFramework,Version=v4.0")]

// 本文件是隔离通信探针，不进入产品工程；不读取 Office、不改变原 COM 线程或释放流程。
internal static class NamedPipeP0Probe
{
    private const int MaxFrameBytes = 1024 * 1024;
    private const int OperationTimeoutMs = 3000;
    private const int CaseTimeoutMs = 10000;
    private static readonly Encoding Utf8 = new UTF8Encoding(false, true);
    private static int payloadAllocations;
    private static int readCalls;
    private static int timedOutReadCompletions;

    private static readonly string[] Cases = {
        "native-duplex", "oversized-length", "negative-length", "zero-length",
        "half-header", "half-body", "reconnect-full-state", "cancel-connect",
        "cancel-read-owner-stop", "client-connect-timeout", "read-deadline-abort"
    };

    private static int Main(string[] args)
    {
        if (args.Length == 0) return RunSuite();
        // 同步 Win32 客户端及每个用例独立进程有兜底时限，避免失败留下阻塞子进程。
        Thread watchdog = new Thread(delegate() {
            Thread.Sleep(CaseTimeoutMs - 1000);
            Console.Error.WriteLine("TIMEOUT mode=" + String.Join(" ", args));
            Environment.Exit(124);
        });
        watchdog.IsBackground = true;
        watchdog.Start();
        try
        {
            if (args.Length == 3 && args[0] == "peer") return RunNativePeer(args[1], args[2]);
            Require(args.Length == 2 && args[0] == "case", "invalid arguments");
            Stopwatch timer = Stopwatch.StartNew();
            RunCase(args[1]);
            Console.WriteLine("PASS " + args[1] + " elapsedMs=" + timer.ElapsedMilliseconds);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("FAIL " + String.Join(" ", args) + " " + ex);
            return 1;
        }
    }

    private static int RunSuite()
    {
        Console.WriteLine("RUNTIME os=" + Environment.OSVersion + " pointerBits=" + (IntPtr.Size * 8)
            + " clr=" + Environment.Version + " mscorlib=" + typeof(object).Assembly.Location);
        Console.WriteLine("COMPILE_TARGET .NETFramework,Version=v4.0; runtime below is the installed CLR, not original .NET4");
        int failed = 0;
        foreach (string name in Cases)
        {
            using (Process child = StartChild("case " + name))
            {
                if (!child.WaitForExit(CaseTimeoutMs))
                {
                    child.Kill();
                    Require(child.WaitForExit(OperationTimeoutMs), "timed-out case did not terminate after Kill");
                    Console.WriteLine("FAIL " + name + " supervisorTimeoutMs=" + CaseTimeoutMs);
                    failed++;
                }
                else
                {
                    Console.Write(child.StandardOutput.ReadToEnd());
                    Console.Write(child.StandardError.ReadToEnd());
                    Console.WriteLine("CASE_EXIT " + name + "=" + child.ExitCode);
                    if (child.ExitCode != 0) failed++;
                }
            }
        }
        Console.WriteLine("SUMMARY passed=" + (Cases.Length - failed) + " failed=" + failed + " total=" + Cases.Length);
        return failed == 0 ? 0 : 1;
    }

    private static void RunCase(string name)
    {
        if (name == "cancel-connect") { CancelConnect(); return; }
        if (name == "cancel-read-owner-stop") { CancelReadOwner(); return; }
        if (name == "client-connect-timeout") { ClientConnectTimeout(); return; }
        if (name == "read-deadline-abort") { ReadDeadlineAbort(); return; }
        string pipeName = "inkeys-p0-" + Guid.NewGuid().ToString("N");
        if (name == "reconnect-full-state")
        {
            // 新实例沿用端点名，重新发送完整状态；不把旧连接的值冒充重连后的快照。
            Exchange(pipeName, "state-a", "doc=A;epoch=1;page=2;title=课件甲", true);
            Exchange(pipeName, "state-b", "doc=B;epoch=2;page=7;title=课件乙", true);
            Console.WriteLine("DETAIL reconnect epochs=1,2 verified independent full states and clean EOF");
            return;
        }
        if (name == "native-duplex")
        {
            Exchange(pipeName, "duplex", "cmd=next;id=17;title=汉字/漢字/emoji😀;path=C:\\课件\\演示.pptx", false);
            Require(readCalls > 4, "fragmented reads not exercised");
            Console.WriteLine("DETAIL native ABI CreateFileW/ReadFile/WriteFile; explicit current SID ACL; managedReadCalls=" + readCalls);
            return;
        }
        using (NamedPipeServerStream server = NewServer(pipeName))
        using (Peer peer = ConnectPeer(server, pipeName, name))
        {
            bool rejected = false;
            try { ReadFrame(server); }
            catch (InvalidDataException)
            {
                Require(name == "oversized-length" || name == "negative-length" || name == "zero-length", "unexpected invalid length");
                Require(payloadAllocations == 0, "invalid length allocated a payload");
                rejected = true;
                Console.WriteLine("DETAIL length rejected before payload allocation; payloadAllocations=0");
            }
            catch (EndOfStreamException ex)
            {
                Require(name == "half-header" || name == "half-body", "unexpected truncated frame");
                Require(payloadAllocations == (name == "half-header" ? 0 : 1), "truncated-frame allocation count wrong");
                Require(ex.Message.IndexOf(name == "half-header" ? "header" : "payload", StringComparison.Ordinal) >= 0, "EOF stage wrong");
                rejected = true;
                Console.WriteLine("DETAIL " + ex.Message + "; partial frame not accepted");
            }
            Require(rejected, "malformed frame accepted");
            peer.RequireSuccess();
        }
    }

    private static NamedPipeServerStream NewServer(string name)
    {
        // .NET4 的 PipeSecurity 构造重载实际参与互通；不放宽到 Everyone。
        // 当前 SID 的同权限验证不能代替普通/管理员双向及不同凭据的 ACL 实机门禁。
        PipeSecurity security = new PipeSecurity();
        security.SetAccessRuleProtection(true, false);
        SecurityIdentifier userSid = WindowsIdentity.GetCurrent().User;
        security.AddAccessRule(new PipeAccessRule(userSid,
            PipeAccessRights.FullControl, AccessControlType.Allow));
        Console.WriteLine("SECURITY explicitUserSid=" + userSid + " allowedRights=" + PipeAccessRights.FullControl
            + " tokenRestricted=" + IsTokenRestricted(WindowsIdentity.GetCurrent().Token));
        return new NamedPipeServerStream(name, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous, 4096, 4096, security);
    }

    private static Peer ConnectPeer(NamedPipeServerStream server, string name, string mode)
    {
        IAsyncResult wait = server.BeginWaitForConnection(null, null);
        Peer peer = new Peer(StartChild("peer " + name + " " + mode));
        try
        {
            Require(wait.AsyncWaitHandle.WaitOne(OperationTimeoutMs), "server connection timeout");
            server.EndWaitForConnection(wait);
            return peer;
        }
        catch
        {
            Console.WriteLine("PEER_DIAGNOSTIC " + peer.Diagnostic());
            server.Dispose();
            if (wait.AsyncWaitHandle.WaitOne(OperationTimeoutMs))
            {
                try { server.EndWaitForConnection(wait); }
                catch (ObjectDisposedException) { }
                catch (IOException) { }
            }
            peer.Dispose();
            throw;
        }
        finally { if (wait.IsCompleted) wait.AsyncWaitHandle.Close(); }
    }

    private static void Exchange(string name, string mode, string expected, bool expectEof)
    {
        using (NamedPipeServerStream server = NewServer(name))
        using (Peer peer = ConnectPeer(server, name, mode))
        {
            string received = ReadFrame(server);
            Require(received == expected, "native-to-managed UTF8 body mismatch");
            WriteFrame(server, "ack=" + expected + ";result=成功✓");
            peer.RequireSuccess();
            if (expectEof)
            {
                byte[] one = new byte[1];
                Require(ReadPart(server, one, 0, 1) == 0, "peer close did not produce EOF");
            }
        }
    }

    private static void CancelConnect()
    {
        using (NamedPipeServerStream server = NewServer("inkeys-p0-" + Guid.NewGuid().ToString("N")))
        using (ManualResetEvent done = new ManualResetEvent(false))
        {
            Exception outcome = null;
            IAsyncResult wait = server.BeginWaitForConnection(delegate(IAsyncResult result) {
                try { server.EndWaitForConnection(result); }
                catch (Exception ex) { outcome = ex; }
                finally { done.Set(); }
            }, null);
            Require(!wait.IsCompleted, "connection wait not pending");
            Stopwatch timer = Stopwatch.StartNew();
            server.Dispose();
            Require(done.WaitOne(OperationTimeoutMs), "Dispose did not complete connection APM");
            Require(wait.IsCompleted && IsStopped(outcome), "connection cancellation not observed");
            Console.WriteLine("DETAIL Dispose->EndWaitForConnection=" + outcome.GetType().Name + " stopMs=" + timer.ElapsedMilliseconds);
        }
    }

    private static void CancelReadOwner()
    {
        string name = "inkeys-p0-" + Guid.NewGuid().ToString("N");
        using (NamedPipeServerStream server = NewServer(name))
        using (NamedPipeClientStream client = new NamedPipeClientStream(".", name, PipeDirection.InOut, PipeOptions.Asynchronous))
        using (ManualResetEvent started = new ManualResetEvent(false))
        using (ManualResetEvent done = new ManualResetEvent(false))
        {
            IAsyncResult connect = server.BeginWaitForConnection(null, null);
            try { client.Connect(OperationTimeoutMs); }
            catch
            {
                server.Dispose();
                if (connect.AsyncWaitHandle.WaitOne(OperationTimeoutMs))
                {
                    try { server.EndWaitForConnection(connect); }
                    catch (ObjectDisposedException) { }
                    catch (IOException) { }
                    connect.AsyncWaitHandle.Close();
                }
                throw;
            }
            Require(connect.AsyncWaitHandle.WaitOne(OperationTimeoutMs), "managed connection timeout");
            server.EndWaitForConnection(connect);
            connect.AsyncWaitHandle.Close();
            Exception outcome = null;
            int returned = -1;
            bool pending = false;
            Thread owner = new Thread(delegate() {
                try
                {
                    byte[] one = new byte[1];
                    IAsyncResult read = server.BeginRead(one, 0, 1, null, null);
                    pending = !read.IsCompleted;
                    started.Set();
                    try
                    {
                        if (!read.AsyncWaitHandle.WaitOne(OperationTimeoutMs)) throw new TimeoutException("owner read not stopped");
                        returned = server.EndRead(read);
                    }
                    finally { read.AsyncWaitHandle.Close(); }
                }
                catch (Exception ex) { outcome = ex; }
                finally { started.Set(); done.Set(); }
            });
            owner.IsBackground = true;
            owner.Start();
            Require(started.WaitOne(OperationTimeoutMs) && pending, "owner did not enter pending read");
            Stopwatch timer = Stopwatch.StartNew();
            server.Dispose();
            Require(done.WaitOne(OperationTimeoutMs), "owner did not leave read after Dispose");
            Require(owner.Join(OperationTimeoutMs), "owner did not join");
            Require(IsStopped(outcome) || (outcome == null && returned == 0), "read cancellation result not EOF/abort");
            Console.WriteLine("DETAIL Dispose->ownerJoin stopMs=" + timer.ElapsedMilliseconds
                + " outcome=" + (outcome == null ? "EOF" : outcome.GetType().Name) + " ownerAlive=" + owner.IsAlive);
        }
    }

    private static bool IsStopped(Exception error)
    {
        return error is ObjectDisposedException || error is IOException;
    }

    private static void ClientConnectTimeout()
    {
        using (NamedPipeClientStream client = new NamedPipeClientStream(".", "inkeys-p0-missing-" + Guid.NewGuid().ToString("N"), PipeDirection.InOut))
        {
            Stopwatch timer = Stopwatch.StartNew();
            bool timedOut = false;
            try { client.Connect(120); }
            catch (TimeoutException) { timedOut = true; }
            Require(timedOut && timer.ElapsedMilliseconds >= 80 && timer.ElapsedMilliseconds < OperationTimeoutMs,
                "Connect(timeout) did not bound absent-peer wait");
            Console.WriteLine("DETAIL Connect(120) actualMs=" + timer.ElapsedMilliseconds + " result=TimeoutException");
        }
    }

    private static void ReadDeadlineAbort()
    {
        string name = "inkeys-p0-" + Guid.NewGuid().ToString("N");
        using (NamedPipeServerStream server = NewServer(name))
        using (Peer peer = ConnectPeer(server, name, "idle"))
        {
            // 对端真实等待且不发数据，覆盖 ReadPart 的 deadline 中止与 EndRead 收束。
            Stopwatch timer = Stopwatch.StartNew();
            bool timedOut = false;
            try { ReadPart(server, new byte[1], 0, 1); }
            catch (TimeoutException) { timedOut = true; }
            Require(timedOut && timedOutReadCompletions == 1, "read deadline did not finalize its completed APM");
            Require(timer.ElapsedMilliseconds >= OperationTimeoutMs - 100
                && timer.ElapsedMilliseconds < 2 * OperationTimeoutMs, "read deadline not bounded");
            peer.RequireSuccess();
            Console.WriteLine("DETAIL pending read deadlineMs=" + OperationTimeoutMs
                + " actualMs=" + timer.ElapsedMilliseconds + " timedOutReadCompletions=" + timedOutReadCompletions);
        }
    }

    private static string ReadFrame(Stream stream)
    {
        byte[] header = new byte[4];
        ReadExactly(stream, header, "header");
        int length = header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
        // 先检查不可信长度，再分配 payload；探针选用 LE int32 + 非空 UTF8，不冻结未来产品协议。
        if (length <= 0 || length > MaxFrameBytes) throw new InvalidDataException("frame length out of range: " + length);
        payloadAllocations++;
        byte[] payload = new byte[length];
        ReadExactly(stream, payload, "payload");
        return Utf8.GetString(payload);
    }

    private static void ReadExactly(Stream stream, byte[] buffer, string stage)
    {
        int used = 0;
        while (used < buffer.Length)
        {
            // 限制每次请求，使头/body 都必须经多次读取拼接，不能依赖一次 Read 返回整帧。
            int count = ReadPart(stream, buffer, used, Math.Min(stage == "header" ? 1 : 3, buffer.Length - used));
            readCalls++;
            if (count == 0) throw new EndOfStreamException(stage + " EOF bytes=" + used + " expected=" + buffer.Length);
            used += count;
        }
    }

    private static int ReadPart(Stream stream, byte[] buffer, int offset, int count)
    {
        if (!(stream is PipeStream)) return stream.Read(buffer, offset, count);
        IAsyncResult read = stream.BeginRead(buffer, offset, count, null, null);
        try
        {
            if (!read.AsyncWaitHandle.WaitOne(OperationTimeoutMs))
            {
                stream.Dispose();
                bool completed = read.AsyncWaitHandle.WaitOne(OperationTimeoutMs);
                if (completed)
                {
                    // Begin/End 必须配对；关闭句柄不代替完成后的 EndRead。
                    try { stream.EndRead(read); }
                    catch (ObjectDisposedException) { }
                    catch (IOException) { }
                    timedOutReadCompletions++;
                }
                throw new TimeoutException(completed ? "read timeout after completed abort" : "read timeout; abort not completed");
            }
            return stream.EndRead(read);
        }
        finally { if (read.IsCompleted) read.AsyncWaitHandle.Close(); }
    }

    private static void WriteFrame(Stream stream, string body)
    {
        byte[] bytes = Utf8.GetBytes(body);
        WriteFragmented(stream, Header(bytes.Length));
        WriteFragmented(stream, bytes);
    }

    private static byte[] Header(int length)
    {
        return new byte[] { (byte)length, (byte)(length >> 8), (byte)(length >> 16), (byte)(length >> 24) };
    }

    private static void WriteFragmented(Stream stream, byte[] bytes)
    {
        for (int offset = 0; offset < bytes.Length; )
        {
            int count = Math.Min(1 + (offset % 3), bytes.Length - offset);
            if (stream is PipeStream)
            {
                IAsyncResult write = stream.BeginWrite(bytes, offset, count, null, null);
                try
                {
                    if (!write.AsyncWaitHandle.WaitOne(OperationTimeoutMs))
                    {
                        stream.Dispose();
                        bool completed = write.AsyncWaitHandle.WaitOne(OperationTimeoutMs);
                        if (completed)
                        {
                            // 超时关闭后先完成 EndWrite，再离开缓冲与 wait handle 的生命周期。
                            try { stream.EndWrite(write); }
                            catch (ObjectDisposedException) { }
                            catch (IOException) { }
                        }
                        throw new TimeoutException(completed ? "write timeout after completed abort" : "write timeout; abort not completed");
                    }
                    stream.EndWrite(write);
                }
                finally { if (write.IsCompleted) write.AsyncWaitHandle.Close(); }
            }
            else stream.Write(bytes, offset, count);
            offset += count;
        }
    }

    private static int RunNativePeer(string name, string mode)
    {
        using (NativePipeStream stream = new NativePipeStream("\\\\.\\pipe\\" + name))
        {
            if (mode == "oversized-length" || mode == "negative-length" || mode == "zero-length")
                WriteFragmented(stream, Header(mode == "oversized-length" ? MaxFrameBytes + 1 : (mode == "negative-length" ? -1 : 0)));
            else if (mode == "half-header") WriteFragmented(stream, new byte[] { 128, 0 });
            else if (mode == "half-body") { WriteFragmented(stream, Header(128)); WriteFragmented(stream, new byte[] { 65, 66 }); }
            else if (mode == "idle")
            {
                Require(stream.Read(new byte[1], 0, 1) == 0, "idle native peer did not observe server close");
            }
            else
            {
                string body = mode == "state-a" ? "doc=A;epoch=1;page=2;title=课件甲" : (mode == "state-b" ?
                    "doc=B;epoch=2;page=7;title=课件乙" : "cmd=next;id=17;title=汉字/漢字/emoji😀;path=C:\\课件\\演示.pptx");
                Require(mode == "state-a" || mode == "state-b" || mode == "duplex", "unknown peer mode");
                WriteFrame(stream, body);
                Require(ReadFrame(stream) == "ack=" + body + ";result=成功✓", "managed-to-native UTF8 ACK mismatch");
                Console.WriteLine("PEER_ACK exact UTF8 duplex verified");
            }
        }
        return 0;
    }

    private static Process StartChild(string arguments)
    {
        ProcessStartInfo start = new ProcessStartInfo(typeof(NamedPipeP0Probe).Assembly.Location, arguments);
        start.UseShellExecute = false;
        start.CreateNoWindow = true;
        start.RedirectStandardOutput = true;
        start.RedirectStandardError = true;
        return Process.Start(start);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool IsTokenRestricted(IntPtr token);

    private sealed class Peer : IDisposable
    {
        private readonly Process process;
        internal Peer(Process value) { process = value; }
        internal string Diagnostic()
        {
            return process.HasExited ? "exit=" + process.ExitCode + " " + process.StandardError.ReadToEnd() : "peer still running";
        }
        internal void RequireSuccess()
        {
            Require(process.WaitForExit(OperationTimeoutMs), "native peer did not exit");
            string output = process.StandardOutput.ReadToEnd();
            string error = process.StandardError.ReadToEnd();
            Require(process.ExitCode == 0, "native peer exit=" + process.ExitCode + " " + error);
            Console.Write(output);
            Console.WriteLine("PEER_EXIT=0");
        }
        public void Dispose()
        {
            try
            {
                if (!process.HasExited)
                {
                    process.Kill();
                    Require(process.WaitForExit(OperationTimeoutMs), "native peer did not terminate after Kill");
                }
            }
            finally { process.Dispose(); }
        }
    }

    private sealed class NativePipeStream : Stream
    {
        private readonly SafeFileHandle handle;
        internal NativePipeStream(string path)
        {
            handle = CreateFileW(path, 0xC0000000, 0, IntPtr.Zero, 3, 0, IntPtr.Zero);
            if (handle.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                throw new Win32Exception(error, "CreateFileW error=" + error + " path=" + path);
            }
        }
        public override int Read(byte[] buffer, int offset, int count)
        {
            byte[] part = new byte[count];
            uint received;
            if (!ReadFile(handle, part, (uint)count, out received, IntPtr.Zero))
            {
                int error = Marshal.GetLastWin32Error();
                if (error == 109 || error == 232) return 0;
                throw new Win32Exception(error, "ReadFile");
            }
            Buffer.BlockCopy(part, 0, buffer, offset, (int)received);
            return (int)received;
        }
        public override void Write(byte[] buffer, int offset, int count)
        {
            while (count > 0)
            {
                byte[] part = new byte[count];
                Buffer.BlockCopy(buffer, offset, part, 0, count);
                uint written;
                if (!WriteFile(handle, part, (uint)count, out written, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error(), "WriteFile");
                Require(written > 0 && written <= count, "invalid native write length");
                offset += (int)written;
                count -= (int)written;
            }
        }
        protected override void Dispose(bool disposing) { if (disposing) handle.Dispose(); base.Dispose(disposing); }
        public override bool CanRead { get { return true; } }
        public override bool CanWrite { get { return true; } }
        public override bool CanSeek { get { return false; } }
        public override long Length { get { throw new NotSupportedException(); } }
        public override long Position { get { throw new NotSupportedException(); } set { throw new NotSupportedException(); } }
        public override void Flush() { }
        public override long Seek(long offset, SeekOrigin origin) { throw new NotSupportedException(); }
        public override void SetLength(long value) { throw new NotSupportedException(); }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string name, uint access, uint sharing, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool ReadFile(SafeFileHandle pipe, byte[] buffer, uint count, out uint received, IntPtr overlapped);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool WriteFile(SafeFileHandle pipe, byte[] buffer, uint count, out uint written, IntPtr overlapped);
    }
}
