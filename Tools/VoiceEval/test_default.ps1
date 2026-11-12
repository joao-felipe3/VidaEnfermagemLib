$src = @'
using System;
using System.Runtime.InteropServices;
namespace AudioLib2 {
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] public class MMDevEnum { }
    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IMMDevEnum {
        int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr ppDevices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDev ppDevice);
    }
    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IMMDev {
        int OpenPropertyStore(int stgmAccess, out IntPtr ppProperties);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string ppstrId);
    }
    public class Tester {
        public static string Get(int role) {
            var e = (IMMDevEnum)(new MMDevEnum());
            IMMDev d;
            int hr = e.GetDefaultAudioEndpoint(1, role, out d);
            if (hr != 0 || d == null) return "Error " + hr;
            string id;
            d.GetId(out id);
            return id;
        }
    }
}
'@
Add-Type -TypeDefinition $src
Write-Host "Default Console: " ([AudioLib2.Tester]::Get(0))
Write-Host "Default Comms:   " ([AudioLib2.Tester]::Get(2))

