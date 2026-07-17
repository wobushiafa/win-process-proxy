# Configuration

`WinProcessProxy.Service` reads `appsettings.json` from its publish directory. The
public configuration contains only the SOCKS5 endpoint, DNS endpoint, and the
process names that should be proxied:

```json
{
  "Socks5": {
    "Host": "127.0.0.1",
    "Port": 1080,
    "Username": "",
    "Password": ""
  },
  "Dns": {
    "Mode": "ProcessOnly",
    "Host": "1.1.1.1",
    "Port": 53
  },
  "Processes": [
    "chrome.exe",
    "example.exe"
  ]
}
```

`Dns.Mode` controls DNS interception independently from general UDP proxying:

- `Disabled` leaves DNS traffic untouched. `Host` and `Port` are ignored.
- `ProcessOnly` proxies traditional UDP port 53 queries from entries in `Processes`.
- `SystemWide` proxies traditional UDP port 53 queries from all processes while
  leaving non-DNS UDP from unlisted processes untouched.

Configurations without `Dns.Mode` remain compatible and use `ProcessOnly`.

`Processes` accepts file names rather than paths or regular expressions.
Matching is case-insensitive and checks the executable name at the end of the
native process path. An empty collection means no processes are proxied.

Environment variables can override values by using double underscores, for
example `Socks5__Host`.

The service watches `appsettings.json`. Valid changes are applied without
restarting the process. A failed reload is logged with `[CONFIG:ERROR]` and the
last working native configuration is restored.

Runtime log messages use review-friendly tags: `[APP]`, `[TCP:OPEN]`,
`[TCP:CLOSE]`, `[TCP:DOMAIN]`, `[TCP:IP-FALLBACK]`, `[TCP:DOMAIN-FAIL]`,
`[DNS]`, and `[CONFIG]`. `[UDP:OPEN]` and `[UDP:CLOSE]` remain available at
the Debug log level.
