using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Threading;

namespace PptCOM
{
    internal static class ComFailure
    {
        public static int ErrorCode(Exception exception)
        {
            while (exception != null)
            {
                var external = exception as ExternalException;
                if (external != null) return external.ErrorCode;
                exception = exception.InnerException;
            }
            // .NET4 的一般 Exception.HResult 不公开；0 明确表示未知。诊断不能设置线程 IErrorInfo。
            return 0;
        }
    }

    // EApplication 是 dispinterface。这里保留完整 IDispatch 槽位，但参数采用原始指针，
    // 不让 CLR 将事件 VARIANT 中的 WnObj 物化为需要 GC 才能配平的新 RCW。
    [ComImport]
    [ComVisible(true)]
    [Guid("914934C2-5A91-11CF-8700-00AA0060263B")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IRawApplicationEvents
    {
        [PreserveSig] int GetTypeInfoCount(out uint count);
        [PreserveSig] int GetTypeInfo(uint index, uint locale, out IntPtr typeInfo);
        [PreserveSig] int GetIDsOfNames(ref Guid iid, IntPtr names, uint count, uint locale, IntPtr ids);
        [PreserveSig] int Invoke(int dispId, ref Guid iid, uint locale, ushort flags,
            IntPtr parameters, IntPtr result, IntPtr exception, IntPtr argumentError);
    }

    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    internal sealed class RawApplicationEventSink : IRawApplicationEvents, ICustomQueryInterface
    {
        private Action<int> notify;
        public RawApplicationEventSink(Action<int> notify) { this.notify = notify; }
        public void Deactivate() { Interlocked.Exchange(ref notify, null); }
        public CustomQueryInterfaceResult GetInterface(ref Guid iid, out IntPtr pointer)
        {
            pointer = IntPtr.Zero;
            if (iid != ComEventConnection.EventInterface &&
                iid != new Guid("00020400-0000-0000-C000-000000000046"))
                return CustomQueryInterfaceResult.NotHandled;
            pointer = Marshal.GetComInterfaceForObject(this, typeof(IRawApplicationEvents),
                CustomQueryInterfaceMode.Ignore);
            return CustomQueryInterfaceResult.Handled;
        }
        public int GetTypeInfoCount(out uint count) { count = 0; return 0; }
        public int GetTypeInfo(uint index, uint locale, out IntPtr typeInfo)
        { typeInfo = IntPtr.Zero; return unchecked((int)0x80004001); }
        public int GetIDsOfNames(ref Guid iid, IntPtr names, uint count, uint locale, IntPtr ids)
        { return unchecked((int)0x80020006); }
        public int Invoke(int dispId, ref Guid iid, uint locale, ushort flags,
            IntPtr parameters, IntPtr result, IntPtr exception, IntPtr argumentError)
        {
            try
            {
                Action<int> callback = Interlocked.CompareExchange(ref notify, null, null);
                if (callback != null) callback(dispId);
            }
            catch { }
            return 0;
        }
    }

    internal sealed class ComEventConnection
    {
        internal static readonly Guid EventInterface = new Guid("914934C2-5A91-11CF-8700-00AA0060263B");
        private readonly Func<int, int> unadvise;
        private readonly Action release;
        private readonly RawApplicationEventSink sink;
        private int cookie;
        private bool disconnected;
        public int LastError { get; private set; }

        // 注入边界也用于 owner cleanup 的逐笔账本；生产由独立 CP acquisition 持有服务器能力。
        internal ComEventConnection(int cookie, Func<int, int> unadvise, Action release,
            Action<int> notify)
        {
            this.cookie = cookie;
            this.unadvise = unadvise;
            this.release = release;
            sink = new RawApplicationEventSink(notify);
        }

        public static ComEventConnection Connect(object application, Action<int> notify)
        {
            IConnectionPoint point = null;
            ComEventConnection connection = null;
            try
            {
                var container = application as IConnectionPointContainer;
                if (container == null) throw new InvalidCastException("Connection point unavailable");
                Guid iid = EventInterface;
                container.FindConnectionPoint(ref iid, out point);
                IConnectionPoint ownedPoint = point;
                connection = new ComEventConnection(0,
                    value => { ownedPoint.Unadvise(value); return 0; },
                    () => Marshal.ReleaseComObject(ownedPoint), notify);
                int cookie;
                point.Advise(connection.sink, out cookie);
                connection.cookie = cookie;
                point = null; // FindConnectionPoint 的唯一取得转移给 connection，cast/闭包只借用。
                return connection;
            }
            catch
            {
                if (connection != null) connection.Deactivate();
                throw;
            }
            finally
            {
                if (point != null) Marshal.ReleaseComObject(point);
            }
        }

        public void Deactivate() { sink.Deactivate(); }

        private static bool IsDisconnected(int error)
        {
            return error == unchecked((int)0x80040200) || // CONNECT_E_NOCONNECTION
                error == unchecked((int)0x80010108) || // RPC_E_DISCONNECTED
                error == unchecked((int)0x800401FD) || // CO_E_OBJNOTCONNECTED
                error == unchecked((int)0x80010007) || // RPC_E_SERVER_DIED
                error == unchecked((int)0x80010012);   // RPC_E_SERVER_DIED_DNE
        }

        public bool TryDisconnect()
        {
            if (disconnected) return true;
            Deactivate();
            int error;
            try { error = unadvise(cookie); }
            catch (COMException exception) { error = exception.ErrorCode; }
            catch (Exception exception)
            {
                LastError = ComFailure.ErrorCode(exception);
                return false;
            }
            if (error < 0 && !IsDisconnected(error)) { LastError = error; return false; }
            // Unadvise 成功或服务器明确断连后，才消费 CP 的取得。失败保留原 cookie 供 owner 重试。
            try { release(); }
            catch (Exception exception)
            {
                LastError = ComFailure.ErrorCode(exception);
                return false;
            }
            disconnected = true;
            LastError = 0;
            return true;
        }
    }

    internal static class ComEventRetirementRegistry
    {
        private static readonly object gate = new object();
        private static readonly List<ComEventConnection> connections = new List<ComEventConnection>();
        private static int inFlight;
        public static int PendingCount { get { lock (gate) { return connections.Count + inFlight; } } }
        public static void Retire(ComEventConnection connection)
        {
            connection.Deactivate();
            lock (gate) { connections.Add(connection); }
        }
        public static void Retry()
        {
            ComEventConnection[] pending;
            lock (gate)
            {
                pending = connections.ToArray();
                connections.Clear();
                inFlight += pending.Length;
            }
            // CP/cookie 的所有权转给当前 owner；锁外 COM。旧 server 被 native reset 后也不会失主。
            foreach (var connection in pending)
            {
                bool cleared = connection.TryDisconnect();
                lock (gate)
                {
                    --inFlight;
                    if (!cleared) connections.Add(connection);
                }
                // 先归还失败 lease 的所有权；控制台不可写也不能让 CP/cookie 失主。
                if (!cleared)
                {
                    try { Console.WriteLine("[PptCOM] event-cleanup-pending=1 hr=0x" + connection.LastError.ToString("X8")); }
                    catch { }
                }
            }
        }
    }
}
