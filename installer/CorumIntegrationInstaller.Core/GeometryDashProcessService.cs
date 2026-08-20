using System.Diagnostics;

namespace CorumIntegrationInstaller.Core;

public interface IGeometryDashProcessService
{
    bool IsRunning();
}

public sealed class GeometryDashProcessService : IGeometryDashProcessService
{
    public bool IsRunning()
    {
        var processes = Process.GetProcessesByName(AppConstants.GeometryDashProcessName);
        try
        {
            return processes.Length > 0;
        }
        finally
        {
            foreach (var process in processes)
            {
                process.Dispose();
            }
        }
    }
}
