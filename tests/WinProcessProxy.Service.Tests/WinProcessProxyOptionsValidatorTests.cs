using Microsoft.Extensions.Options;
using Xunit;

namespace WinProcessProxy.Service.Tests;

public sealed class WinProcessProxyOptionsValidatorTests
{
    private readonly WinProcessProxyOptionsValidator _validator = new();

    [Fact]
    public void Validate_DefaultOptions_Succeeds()
    {
        var result = _validator.Validate(Options.DefaultName, new WinProcessProxyOptions());

        Assert.True(result.Succeeded);
    }

    [Fact]
    public void Validate_InvalidSocks5AndDns_FailsWithAllRelevantKeys()
    {
        var options = new WinProcessProxyOptions
        {
            Socks5 = new Socks5Options { Host = "", Port = 0 },
            Dns = new DnsOptions { Host = "resolver.example", Port = 0 }
        };

        var result = _validator.Validate(Options.DefaultName, options);

        Assert.True(result.Failed);
        Assert.Contains(result.Failures!, message => message.Contains("Socks5:Host", StringComparison.Ordinal));
        Assert.Contains(result.Failures!, message => message.Contains("Socks5:Port", StringComparison.Ordinal));
        Assert.Contains(result.Failures!, message => message.Contains("Dns:Host", StringComparison.Ordinal));
        Assert.Contains(result.Failures!, message => message.Contains("Dns:Port", StringComparison.Ordinal));
    }

    [Fact]
    public void Validate_EmptyProcessName_Fails()
    {
        var options = new WinProcessProxyOptions { Processes = [""] };

        var result = _validator.Validate(Options.DefaultName, options);

        Assert.True(result.Failed);
        Assert.Contains(result.Failures!, message => message.Contains("Processes:0", StringComparison.Ordinal));
    }

    [Fact]
    public void Validate_ProcessPath_Fails()
    {
        var options = new WinProcessProxyOptions { Processes = [@"C:\Apps\example.exe"] };

        var result = _validator.Validate(Options.DefaultName, options);

        Assert.True(result.Failed);
        Assert.Contains(result.Failures!, message => message.Contains("not a path", StringComparison.Ordinal));
    }
}
