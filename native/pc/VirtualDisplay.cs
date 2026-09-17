using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

namespace WiredScreen {
    public sealed class VirtualDisplayTarget {
        public string DeviceName;
        public string DeviceString;
        public bool AttachedToDesktop;
    }
    public static class DisplayTopology {
        [DllImport("WiredScreen.Native.dll",CharSet=CharSet.Unicode,ExactSpelling=true)]
        public static extern int WiredScreenFindOutput(string name,out uint adapter,out uint output);
        [DllImport("user32.dll",ExactSpelling=true)]
        private static extern int SetDisplayConfig(uint paths,IntPtr path,uint modes,IntPtr mode,uint flags);
        public static void ExtendDesktop() {
            int result=SetDisplayConfig(0,IntPtr.Zero,0,IntPtr.Zero,0x84); // SDC_APPLY | SDC_TOPOLOGY_EXTEND
            if(result!=0)throw new Win32Exception(result,"Windows 无法启用扩展桌面。");
        }
        public static void CloneDesktop() {
            int result=SetDisplayConfig(0,IntPtr.Zero,0,IntPtr.Zero,0x82); // SDC_APPLY | SDC_TOPOLOGY_CLONE
            if(result!=0)throw new Win32Exception(result,"Windows 无法启用复制桌面。请先确认虚拟副屏已经创建，并选择两台显示器都支持的分辨率。");
        }
        private const uint AttachedToDesktop=0x00000001;
        [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
        private struct DisplayDevice {
            public int cb;
            [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string DeviceName;
            [MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)] public string DeviceString;
            public int StateFlags;
            [MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)] public string DeviceId;
            [MarshalAs(UnmanagedType.ByValTStr,SizeConst=128)] public string DeviceKey;
        }
        [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
        private static extern bool EnumDisplayDevices(string deviceName,uint deviceNumber,ref DisplayDevice displayDevice,uint flags);
        public static VirtualDisplayTarget WaitForSampleDisplay(int timeoutMilliseconds) {
            DateTime deadline=DateTime.UtcNow.AddMilliseconds(timeoutMilliseconds);
            do {
                for(uint index=0;;index++) {
                    DisplayDevice device=new DisplayDevice();device.cb=Marshal.SizeOf(typeof(DisplayDevice));
                    if(!EnumDisplayDevices(null,index,ref device,0))break;
                    string identity=(device.DeviceString??String.Empty)+" "+(device.DeviceId??String.Empty);
                    if(identity.IndexOf("IddSample",StringComparison.OrdinalIgnoreCase)>=0) {
                        return new VirtualDisplayTarget{DeviceName=device.DeviceName,DeviceString=device.DeviceString,AttachedToDesktop=(device.StateFlags&AttachedToDesktop)!=0};
                    }
                }
                Thread.Sleep(200);
            } while(DateTime.UtcNow<deadline);
            return null;
        }
    }
    // This class creates the software device consumed by the Microsoft IddCx
    // reference driver. The handle stays open for the lifetime of the virtual
    // monitor, so closing it cleanly unplugs the Windows display.
    public sealed class VirtualDisplayController : IDisposable {
        private const uint Removable=0x00000001;
        private const uint SilentInstall=0x00000002;
        private const uint DriverRequired=0x00000008;
        private IntPtr device=IntPtr.Zero;
        private CreationCallback callback;
        private CreationState state;
        private VirtualDisplayTarget target;

        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
        private struct CreateInfo {
            public uint cbSize;
            [MarshalAs(UnmanagedType.LPWStr)] public string instanceId;
            [MarshalAs(UnmanagedType.LPWStr)] public string hardwareIds;
            [MarshalAs(UnmanagedType.LPWStr)] public string compatibleIds;
            public IntPtr containerId;
            public uint capabilityFlags;
            [MarshalAs(UnmanagedType.LPWStr)] public string description;
            [MarshalAs(UnmanagedType.LPWStr)] public string location;
            public IntPtr securityDescriptor;
        }
        [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet=CharSet.Unicode)]
        private delegate void CreationCallback(IntPtr swDevice,int creationResult,IntPtr context,[MarshalAs(UnmanagedType.LPWStr)] string instanceId);
        private sealed class CreationState { public readonly ManualResetEvent Done=new ManualResetEvent(false); public int Result; }
        [DllImport("WiredScreen.Native.dll",EntryPoint="WiredScreenSwDeviceCreate",CharSet=CharSet.Unicode,ExactSpelling=true)]
        private static extern int SwDeviceCreate(string enumerator,string parent,ref CreateInfo info,uint propertyCount,IntPtr properties,CreationCallback callback,IntPtr context,out IntPtr device);
        [DllImport("cfgmgr32.dll",ExactSpelling=true)]
        private static extern void SwDeviceClose(IntPtr device);

        public bool IsRunning { get { return device!=IntPtr.Zero; } }
        public VirtualDisplayTarget Start() {
            if(IsRunning)return target;
            state=new CreationState();callback=Created;
            GCHandle handle=GCHandle.Alloc(state);
            try {
                CreateInfo info=new CreateInfo {
                    cbSize=(uint)Marshal.SizeOf(typeof(CreateInfo)),
                    instanceId="WiredScreenVirtualDisplay",
                    // These IDs deliberately match the official IddSampleDriver INF.
                    hardwareIds="IddSampleDriver\0\0",compatibleIds="IddSampleDriver\0\0",
                    capabilityFlags=Removable|SilentInstall|DriverRequired,
                    description="WiredScreen Virtual Display"
                };
                int hr=SwDeviceCreate("IddSampleDriver","HTREE\\ROOT\\0",ref info,0,IntPtr.Zero,callback,GCHandle.ToIntPtr(handle),out device);
                if(hr<0)throw new Win32Exception(hr,"无法创建虚拟显示设备 (HRESULT 0x"+hr.ToString("X8")+")。");
                if(!state.Done.WaitOne(10000))throw new TimeoutException("等待 Windows 加载虚拟显示驱动超时。");
                if(state.Result<0)throw new Win32Exception(state.Result,"Windows 未能加载虚拟显示驱动。");
                target=DisplayTopology.WaitForSampleDisplay(10000);
                if(target==null)throw new IOException("Windows 已接受设备请求，但 10 秒内没有枚举 IddSample 显示器。");
                return target;
            } catch {
                if(device!=IntPtr.Zero){SwDeviceClose(device);device=IntPtr.Zero;}
                throw;
            } finally { handle.Free(); }
        }
        private static void Created(IntPtr swDevice,int result,IntPtr context,string instanceId) {
            GCHandle handle=GCHandle.FromIntPtr(context);CreationState state=(CreationState)handle.Target;
            state.Result=result;state.Done.Set();
        }
        public void Dispose() {
            if(device!=IntPtr.Zero){SwDeviceClose(device);device=IntPtr.Zero;}
            if(state!=null){state.Done.Dispose();state=null;}callback=null;target=null;
        }
    }
}
