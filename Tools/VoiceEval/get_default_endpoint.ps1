$source = @"
using System;
using System.Runtime.InteropServices;

namespace AudioLib
{
    [ComImport]
    [Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
    public class MMDeviceEnumeratorComObject { }

    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IMMDeviceEnumerator
    {
        int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr ppDevices);
        [PreserveSig]
        int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice ppDevice);
    }

    [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IMMDevice
    {
        [PreserveSig]
        int OpenPropertyStore(int stgmAccess, out IPropertyStore ppProperties);
        [PreserveSig]
        int GetId([MarshalAs(UnmanagedType.LPWStr)] out string ppstrId);
    }

    [Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IPropertyStore
    {
        [PreserveSig]
        int GetCount(out int cProps);
        [PreserveSig]
        int GetAt(int iProp, out IntPtr pkey);
        [PreserveSig]
        int GetValue(ref PropertyKey key, out PropVariant pv);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PropertyKey
    {
        public Guid fmtid;
        public int pid;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct PropVariant
    {
        [FieldOffset(0)] public short vt;
        [FieldOffset(8)] public IntPtr pwszVal;
    }

    public class AudioHelper
    {
        public static string GetDefaultEndpointName(int dataFlow, int role)
        {
            var enumerator = (IMMDeviceEnumerator)(new MMDeviceEnumeratorComObject());
            IMMDevice dev;
            int hr = enumerator.GetDefaultAudioEndpoint(dataFlow, role, out dev);
            if (hr != 0 || dev == null) return "Erro: " + hr;

            string devId;
            dev.GetId(out devId);

            IPropertyStore store;
            dev.OpenPropertyStore(0, out store);

            var key = new PropertyKey
            {
                fmtid = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"),
                pid = 14
            };

            PropVariant pv;
            store.GetValue(ref key, out pv);
            string name = Marshal.PtrToStringUni(pv.pwszVal);

            return name + " (" + devId + ")";
        }
    }
}
"@

Add-Type -TypeDefinition $source
Write-Host "Default Capture (Console): " ([AudioLib.AudioHelper]::GetDefaultEndpointName(1, 0))
Write-Host "Default Capture (Comms):   " ([AudioLib.AudioHelper]::GetDefaultEndpointName(1, 2))

