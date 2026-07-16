using System.Runtime.InteropServices;

namespace WinProcessProxy.Service;

internal static partial class NativeMethods
{
    private const string LibraryName = "WinProcessProxy.Native";

    [LibraryImport(LibraryName, EntryPoint = "aio_setLogCallback")]
    internal static partial void SetLogCallback(nint callback);

    [LibraryImport(LibraryName, EntryPoint = "aio_dial", StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int Configure(WinProcessProxyOption option, string value);

    [LibraryImport(LibraryName, EntryPoint = "aio_init")]
    internal static partial int Initialize();

    [LibraryImport(LibraryName, EntryPoint = "aio_free")]
    internal static partial void Free();

    [LibraryImport(LibraryName, EntryPoint = "aio_getUP")]
    internal static partial ulong GetUploadedBytes();

    [LibraryImport(LibraryName, EntryPoint = "aio_getDL")]
    internal static partial ulong GetDownloadedBytes();
}

internal enum WinProcessProxyOption
{
    FilterLoopback,
    FilterIntranet,
    FilterParent,
    FilterIcmp,
    FilterTcp,
    FilterUdp,
    FilterDns,
    IcmpPing,
    DnsOnly,
    DnsProxy,
    DnsHost,
    DnsPort,
    TargetHost,
    TargetPort,
    TargetUser,
    TargetPassword,
    ClearPatterns,
    IncludePattern,
    BypassPattern
}
