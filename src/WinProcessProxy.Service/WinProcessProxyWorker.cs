using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.Extensions.Primitives;

namespace WinProcessProxy.Service;

internal sealed class WinProcessProxyWorker(
    WinProcessProxyClient client,
    IOptionsMonitor<WinProcessProxyOptions> options,
    IConfiguration configuration,
    IHostApplicationLifetime lifetime,
    ILogger<WinProcessProxyWorker> logger) : BackgroundService
{
    private readonly SemaphoreSlim reloadSignal = new(0, 1);

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var activeSettings = options.CurrentValue;
        using var reloadRegistration = ChangeToken.OnChange(
            configuration.GetReloadToken,
            SignalReload);

        try
        {
            if (!client.Initialize(activeSettings))
                throw new InvalidOperationException("Native process proxy initialization failed.");

            while (!stoppingToken.IsCancellationRequested)
            {
                await reloadSignal.WaitAsync(stoppingToken);
                await Task.Delay(TimeSpan.FromMilliseconds(300), stoppingToken);
                while (reloadSignal.Wait(0)) { }

                WinProcessProxyOptions updatedSettings;
                try
                {
                    updatedSettings = options.CurrentValue;
                }
                catch (OptionsValidationException exception)
                {
                    logger.LogError(exception, "[CONFIG:ERROR] Configuration reload rejected.");
                    continue;
                }

                client.Dispose();
                try
                {
                    if (!client.Initialize(updatedSettings))
                        throw new InvalidOperationException("Native process proxy reload failed.");

                    activeSettings = updatedSettings;
                    logger.LogInformation("[CONFIG] Configuration reloaded.");
                }
                catch (Exception exception)
                {
                    logger.LogError(exception, "[CONFIG:ERROR] Reload failed; restoring previous configuration.");
                    if (!client.Initialize(activeSettings))
                        throw new InvalidOperationException("Could not restore the previous native configuration.", exception);
                }
            }
        }
        catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            logger.LogCritical(exception, "[ERROR] WinProcessProxy failed to start or stopped unexpectedly.");
            Environment.ExitCode = 1;
            lifetime.StopApplication();
        }
        finally
        {
            client.Dispose();
        }
    }

    private void SignalReload()
    {
        try
        {
            reloadSignal.Release();
        }
        catch (SemaphoreFullException)
        {
        }
    }
}
