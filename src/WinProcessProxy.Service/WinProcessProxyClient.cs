using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging;

namespace WinProcessProxy.Service;

internal sealed class WinProcessProxyClient : IDisposable
{
    private static ILogger<WinProcessProxyClient>? applicationLogger;
    private bool _initialized;

    public WinProcessProxyClient(ILogger<WinProcessProxyClient> logger)
    {
        applicationLogger = logger;
    }

    public ulong UploadedBytes => NativeMethods.GetUploadedBytes();
    public ulong DownloadedBytes => NativeMethods.GetDownloadedBytes();

    public unsafe bool Initialize(WinProcessProxyOptions options)
    {
        if (_initialized)
            throw new InvalidOperationException("WinProcessProxy is already initialized.");

        Apply(options);
        NativeMethods.SetLogCallback(
            (nint)(delegate* unmanaged[Cdecl]<int, uint, char*, void>)&LogProxyEvent);
        _initialized = NativeMethods.Initialize() != 0;

        if (!_initialized)
            NativeMethods.SetLogCallback(0);

        return _initialized;
    }

    public void Dispose()
    {
        if (!_initialized)
            return;

        NativeMethods.Free();
        NativeMethods.SetLogCallback(0);
        _initialized = false;
    }

    private static void Apply(WinProcessProxyOptions options)
    {
        Set(WinProcessProxyOption.FilterLoopback, false);
        Set(WinProcessProxyOption.FilterIntranet, false);
        Set(WinProcessProxyOption.FilterParent, false);
        Set(WinProcessProxyOption.FilterIcmp, false);
        Set(WinProcessProxyOption.FilterTcp, true);
        Set(WinProcessProxyOption.FilterUdp, true);
        Set(WinProcessProxyOption.FilterDns, true);
        Set(WinProcessProxyOption.IcmpPing, 0U);
        Set(WinProcessProxyOption.DnsOnly, false);
        Set(WinProcessProxyOption.DnsProxy, true);
        Set(WinProcessProxyOption.DnsHost, options.Dns.Host);
        Set(WinProcessProxyOption.DnsPort, options.Dns.Port);
        Set(WinProcessProxyOption.TargetHost, options.Socks5.Host);
        Set(WinProcessProxyOption.TargetPort, options.Socks5.Port);
        Set(WinProcessProxyOption.TargetUser, options.Socks5.Username);
        Set(WinProcessProxyOption.TargetPassword, options.Socks5.Password);
        Set(WinProcessProxyOption.ClearPatterns, string.Empty);

        foreach (var process in options.Processes)
        {
            var processPattern = $"(?:^|[\\\\/]){Regex.Escape(process)}$";
            Set(WinProcessProxyOption.IncludePattern, processPattern);
        }
    }

    private static void Set(WinProcessProxyOption option, bool value) =>
        Set(option, value ? "true" : "false");

    private static void Set(WinProcessProxyOption option, ushort value) =>
        Set(option, value.ToString(CultureInfo.InvariantCulture));

    private static void Set(WinProcessProxyOption option, uint value) =>
        Set(option, value.ToString(CultureInfo.InvariantCulture));

    private static void Set(WinProcessProxyOption option, string value)
    {
        if (NativeMethods.Configure(option, value) == 0)
            throw new InvalidOperationException($"Native configuration rejected {option}.");
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void LogProxyEvent(int eventType, uint processId, char* message)
    {
        try
        {
            var tag = eventType switch
            {
                1 => "[APP]",
                2 => "[TCP:OPEN]",
                3 => "[TCP:CLOSE]",
                4 => "[UDP:OPEN]",
                5 => "[UDP:CLOSE]",
                6 => "[DNS]",
                _ => "[NATIVE]"
            };

            applicationLogger?.LogInformation(
                "{Tag} {Message} (PID {ProcessId})",
                tag,
                new string(message),
                processId);
        }
        catch
        {
            // Exceptions must not cross the unmanaged callback boundary.
        }
    }
}
