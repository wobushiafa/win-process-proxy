namespace WinProcessProxy.Service;

public sealed class WinProcessProxyOptions
{
    public Socks5Options Socks5 { get; set; } = new();
    public DnsOptions Dns { get; set; } = new();
    public string[] Processes { get; set; } = [];
}

public sealed class Socks5Options
{
    public string Host { get; set; } = "127.0.0.1";
    public ushort Port { get; set; } = 1080;
    public string Username { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;
}

public sealed class DnsOptions
{
    public string Host { get; set; } = "1.1.1.1";
    public ushort Port { get; set; } = 53;
}
