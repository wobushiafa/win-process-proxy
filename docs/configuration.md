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
    "Host": "1.1.1.1",
    "Port": 53
  },
  "Processes": [
    "chrome.exe",
    "example.exe"
  ]
}
```

`Processes` accepts file names rather than paths or regular expressions.
Matching is case-insensitive and checks the executable name at the end of the
native process path. An empty collection means no processes are proxied.

Environment variables can override values by using double underscores, for
example `Socks5__Host`.

The service watches `appsettings.json`. Valid changes are applied without
restarting the process. A failed reload is logged with `[CONFIG:ERROR]` and the
last working native configuration is restored.

Runtime log messages use review-friendly tags: `[APP]`, `[TCP:OPEN]`,
`[TCP:CLOSE]`, `[UDP:OPEN]`, `[UDP:CLOSE]`, `[DNS]`, and `[CONFIG]`.
