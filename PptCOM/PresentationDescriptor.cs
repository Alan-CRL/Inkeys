using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Web.Script.Serialization;

namespace PptCOM
{
    internal sealed class PresentationDescriptorValue
    {
        public int schemaVersion { get; set; }
        public string provider { get; set; }
        public string status { get; set; }
        public string fullName { get; set; }
        public string presentationName { get; set; }
        public int applicationProcessId { get; set; }
        public long slideShowHwnd { get; set; }
        public int currentPage { get; set; }
        public int totalPage { get; set; }
        public int? currentSlideId { get; set; }
        public int[] slideIds { get; set; }
        public long bindingRevision { get; set; }

        public PresentationDescriptorValue Clone()
        {
            return new PresentationDescriptorValue
            {
                schemaVersion = schemaVersion,
                provider = provider,
                status = status,
                fullName = fullName,
                presentationName = presentationName,
                applicationProcessId = applicationProcessId,
                slideShowHwnd = slideShowHwnd,
                currentPage = currentPage,
                totalPage = totalPage,
                currentSlideId = currentSlideId,
                slideIds = slideIds == null ? new int[0] : (int[])slideIds.Clone(),
                bindingRevision = bindingRevision
            };
        }

        public static PresentationDescriptorValue CreateStatus(
            string status, long bindingRevision)
        {
            return new PresentationDescriptorValue
            {
                schemaVersion = 1,
                provider = "Unknown",
                status = status,
                fullName = string.Empty,
                presentationName = string.Empty,
                slideIds = new int[0],
                bindingRevision = bindingRevision
            };
        }
    }

    internal interface ILateBoundComAccessor
    {
        object GetProperty(object target, string name);
        object GetItem(object collection, int oneBasedIndex);
        bool IsComObject(object value);
        void Release(object value);
    }

    internal sealed class MarshalLateBoundComAccessor : ILateBoundComAccessor
    {
        private static object Invoke(object target, string name,
            BindingFlags flags, object[] arguments)
        {
            if (target == null) throw new ArgumentNullException("target");
            try
            {
                return target.GetType().InvokeMember(name,
                    flags | BindingFlags.Public | BindingFlags.Instance |
                    BindingFlags.OptionalParamBinding, null, target, arguments,
                    CultureInfo.InvariantCulture);
            }
            catch (TargetInvocationException exception)
            {
                throw exception.InnerException ?? exception;
            }
        }

        public object GetProperty(object target, string name)
        {
            return Invoke(target, name, BindingFlags.GetProperty, null);
        }

        public object GetItem(object collection, int oneBasedIndex)
        {
            return Invoke(collection, "Item",
                BindingFlags.GetProperty | BindingFlags.InvokeMethod,
                new object[] { oneBasedIndex });
        }

        public bool IsComObject(object value)
        {
            return value != null && Marshal.IsComObject(value);
        }

        public void Release(object value)
        {
            if (!IsComObject(value)) return;
            try
            {
                Marshal.ReleaseComObject(value);
            }
            catch
            {
            }
        }
    }

    internal sealed class PresentationDescriptorReader
    {
        private const int MaximumIdentityUtf8Bytes = 32768;
        private const int MaximumSlides = 10000;
        private readonly ILateBoundComAccessor accessor;
        private readonly Func<IntPtr, int> processIdResolver;

        public PresentationDescriptorReader(ILateBoundComAccessor accessor,
            Func<IntPtr, int> processIdResolver)
        {
            if (accessor == null) throw new ArgumentNullException("accessor");
            this.accessor = accessor;
            this.processIdResolver = processIdResolver;
        }

        private static bool IsBusy(Exception exception)
        {
            Exception current = exception;
            while (current != null)
            {
                COMException com = current as COMException;
                if (com != null)
                {
                    uint code = unchecked((uint)com.ErrorCode);
                    if (code == 0x8001010A || code == 0x800AC472 ||
                        code == 0x80010001) return true;
                }
                current = current.InnerException;
            }
            return false;
        }

        private object GetScalar(object target, string name)
        {
            object value = null;
            try
            {
                value = accessor.GetProperty(target, name);
                if (accessor.IsComObject(value))
                    throw new InvalidCastException(name + " returned a COM object");
                return value;
            }
            finally
            {
                if (accessor.IsComObject(value)) accessor.Release(value);
            }
        }

        private int GetInt32(object target, string name)
        {
            return Convert.ToInt32(GetScalar(target, name),
                CultureInfo.InvariantCulture);
        }

        private long GetInt64(object target, string name)
        {
            object value = GetScalar(target, name);
            if (value is IntPtr) return ((IntPtr)value).ToInt64();
            return Convert.ToInt64(value, CultureInfo.InvariantCulture);
        }

        private string TryGetString(object target, string name)
        {
            try
            {
                object value = GetScalar(target, name);
                string result = Convert.ToString(value,
                    CultureInfo.InvariantCulture) ?? string.Empty;
                // 损坏的 IDispatch 也不能让 owner 缓存或序列化无界身份字符串。
                if (Encoding.UTF8.GetByteCount(result) > MaximumIdentityUtf8Bytes)
                    throw new ArgumentOutOfRangeException(name,
                        "Identity exceeds the UTF-8 byte limit");
                return result;
            }
            catch (Exception exception)
            {
                if (IsBusy(exception) || exception is ArgumentOutOfRangeException) throw;
                return string.Empty;
            }
        }

        private long TryGetHwnd(object slideShowWindow)
        {
            try
            {
                return GetInt64(slideShowWindow, "HWND");
            }
            catch (Exception exception)
            {
                if (IsBusy(exception)) throw;
                return 0;
            }
        }

        private static string ResolveProvider(string applicationName,
            string applicationCaption)
        {
            string value = (applicationName + " " + applicationCaption).ToUpperInvariant();
            if (value.IndexOf("WPS", StringComparison.Ordinal) >= 0 ||
                value.IndexOf("WPP", StringComparison.Ordinal) >= 0)
                return "Wps";
            if (value.IndexOf("POWERPOINT", StringComparison.Ordinal) >= 0)
                return "PowerPoint";
            return "Unknown";
        }

        public PresentationDescriptorValue Read(object application,
            object presentation, object slideShowWindow, long bindingRevision)
        {
            PresentationDescriptorValue descriptor =
                PresentationDescriptorValue.CreateStatus("Unavailable", bindingRevision);
            if (application == null || presentation == null || slideShowWindow == null)
                return descriptor;

            try
            {
                string applicationName = TryGetString(application, "Name");
                string applicationCaption = TryGetString(application, "Caption");
                descriptor.provider = ResolveProvider(applicationName, applicationCaption);
                descriptor.fullName = TryGetString(presentation, "FullName");
                descriptor.presentationName = TryGetString(presentation, "Name");
                if (descriptor.fullName.Length == 0 &&
                    descriptor.presentationName.Length == 0) return descriptor;
                descriptor.slideShowHwnd = TryGetHwnd(slideShowWindow);
                if (descriptor.slideShowHwnd != 0 && processIdResolver != null)
                    descriptor.applicationProcessId = processIdResolver(
                        new IntPtr(descriptor.slideShowHwnd));
            }
            catch (Exception exception)
            {
                descriptor.status = IsBusy(exception) ? "TransientBusy" : "Unavailable";
                return descriptor;
            }

            bool hasCurrentSlideId = false;
            int currentSlideId = 0;
            object view = null;
            object currentSlide = null;
            try
            {
                view = accessor.GetProperty(slideShowWindow, "View");
                currentSlide = accessor.GetProperty(view, "Slide");
                descriptor.currentPage = GetInt32(currentSlide, "SlideIndex");
                try
                {
                    currentSlideId = GetInt32(currentSlide, "SlideID");
                    hasCurrentSlideId = currentSlideId > 0;
                }
                catch (Exception exception)
                {
                    if (IsBusy(exception)) throw;
                    hasCurrentSlideId = false;
                }
            }
            catch (Exception exception)
            {
                descriptor.status = IsBusy(exception) ? "TransientBusy" : "Unavailable";
                return descriptor;
            }
            finally
            {
                // 每次 object-valued COM 返回都是独立 acquisition，按子到父释放。
                accessor.Release(currentSlide);
                accessor.Release(view);
            }

            object slides = null;
            try
            {
                slides = accessor.GetProperty(presentation, "Slides");
                descriptor.totalPage = GetInt32(slides, "Count");
                if (descriptor.totalPage <= 0 || descriptor.currentPage <= 0 ||
                    descriptor.currentPage > descriptor.totalPage ||
                    descriptor.totalPage > MaximumSlides)
                    return PresentationDescriptorValue.CreateStatus(
                        "Unavailable", bindingRevision);

                if (!hasCurrentSlideId)
                {
                    descriptor.status = "PageIndexFallback";
                    return descriptor;
                }

                int[] slideIds = new int[descriptor.totalPage];
                HashSet<int> uniqueIds = new HashSet<int>();
                for (int index = 1; index <= descriptor.totalPage; ++index)
                {
                    object slide = null;
                    try
                    {
                        slide = accessor.GetItem(slides, index);
                        int slideIndex = GetInt32(slide, "SlideIndex");
                        int slideId = GetInt32(slide, "SlideID");
                        if (slideIndex != index || slideId <= 0 ||
                            !uniqueIds.Add(slideId))
                            throw new InvalidOperationException("Invalid SlideID topology");
                        slideIds[index - 1] = slideId;
                    }
                    finally
                    {
                        accessor.Release(slide);
                    }
                }
                if (slideIds[descriptor.currentPage - 1] != currentSlideId)
                    throw new InvalidOperationException("Current SlideID does not match topology");

                descriptor.currentSlideId = currentSlideId;
                descriptor.slideIds = slideIds;
                descriptor.status = "StableSlideIds";
                return descriptor;
            }
            catch (Exception exception)
            {
                if (IsBusy(exception))
                {
                    descriptor.status = "TransientBusy";
                    descriptor.currentSlideId = null;
                    descriptor.slideIds = new int[0];
                    return descriptor;
                }
                // 当前页序号来自同一次 View.Slide；拓扑失败时只做显式页码退化。
                if (descriptor.currentPage > 0 && descriptor.totalPage >= descriptor.currentPage)
                {
                    descriptor.status = "PageIndexFallback";
                    descriptor.currentSlideId = null;
                    descriptor.slideIds = new int[0];
                    return descriptor;
                }
                return PresentationDescriptorValue.CreateStatus(
                    "Unavailable", bindingRevision);
            }
            finally
            {
                accessor.Release(slides);
            }
        }
    }

    internal static class PresentationDescriptorJson
    {
        public static string Serialize(PresentationDescriptorValue descriptor)
        {
            JavaScriptSerializer serializer = new JavaScriptSerializer();
            serializer.MaxJsonLength = 1024 * 1024;
            return serializer.Serialize(descriptor);
        }

        public static string SerializeUnavailable(long bindingRevision)
        {
            try
            {
                return Serialize(PresentationDescriptorValue.CreateStatus(
                    "Unavailable", bindingRevision));
            }
            catch
            {
                try
                {
                    // 最后回退不依赖 serializer，避免 COM getter 把托管异常穿过 ABI。
                    return "{\"schemaVersion\":1,\"provider\":\"Unknown\"," +
                        "\"status\":\"Unavailable\",\"fullName\":\"\"," +
                        "\"presentationName\":\"\",\"applicationProcessId\":0," +
                        "\"slideShowHwnd\":0,\"currentPage\":0,\"totalPage\":0," +
                        "\"currentSlideId\":null,\"slideIds\":[],\"bindingRevision\":" +
                        bindingRevision.ToString(CultureInfo.InvariantCulture) + "}";
                }
                catch
                {
                    return "{\"schemaVersion\":1,\"provider\":\"Unknown\",\"status\":\"Unavailable\",\"fullName\":\"\",\"presentationName\":\"\",\"applicationProcessId\":0,\"slideShowHwnd\":0,\"currentPage\":0,\"totalPage\":0,\"currentSlideId\":null,\"slideIds\":[],\"bindingRevision\":0}";
                }
            }
        }
    }
}
