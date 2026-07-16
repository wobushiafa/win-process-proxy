using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using WinProcessProxy.Service;

if (!OperatingSystem.IsWindows())
{
    Console.Error.WriteLine("WinProcessProxy.Service only supports Windows x64.");
    return 1;
}

var builder = Host.CreateApplicationBuilder(args);
builder.Logging.AddFilter("Microsoft.Hosting.Lifetime", LogLevel.Warning);

builder.Services.AddWindowsService(options => options.ServiceName = "WinProcessProxy");
builder.Services
    .AddOptions<WinProcessProxyOptions>()
    .Bind(builder.Configuration)
    .ValidateOnStart();
builder.Services.AddSingleton<IValidateOptions<WinProcessProxyOptions>, WinProcessProxyOptionsValidator>();
builder.Services.AddSingleton<WinProcessProxyClient>();
builder.Services.AddHostedService<WinProcessProxyWorker>();

await builder.Build().RunAsync();
return Environment.ExitCode;
