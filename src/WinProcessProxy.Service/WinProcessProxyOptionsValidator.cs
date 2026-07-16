using System.Net;
using Microsoft.Extensions.Options;

namespace WinProcessProxy.Service;

internal sealed class WinProcessProxyOptionsValidator : IValidateOptions<WinProcessProxyOptions>
{
    public ValidateOptionsResult Validate(string? name, WinProcessProxyOptions options)
    {
        var failures = new List<string>();

        Require(options.Socks5.Host, "Socks5:Host", failures);
        if (options.Socks5.Port == 0)
            failures.Add("Socks5:Port must be between 1 and 65535.");

        if (!IPAddress.TryParse(options.Dns.Host, out _))
            failures.Add("Dns:Host must be an IPv4 or IPv6 address.");
        if (options.Dns.Port == 0)
            failures.Add("Dns:Port must be between 1 and 65535.");

        for (var index = 0; index < options.Processes.Length; index++)
        {
            var process = options.Processes[index];
            if (string.IsNullOrWhiteSpace(process))
            {
                failures.Add($"Processes:{index} must not be empty.");
            }
            else if (Path.GetFileName(process) != process)
            {
                failures.Add($"Processes:{index} must be a process file name, not a path.");
            }
        }

        return failures.Count == 0
            ? ValidateOptionsResult.Success
            : ValidateOptionsResult.Fail(failures);
    }

    private static void Require(string value, string key, ICollection<string> failures)
    {
        if (string.IsNullOrWhiteSpace(value))
            failures.Add($"{key} is required.");
    }
}
