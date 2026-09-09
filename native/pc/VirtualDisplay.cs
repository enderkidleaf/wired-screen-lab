using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;

namespace WiredScreen {
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
        [DllImport("swdevice.dll",CharSet=CharSet.Unicode,ExactSpelling=true)]
        private static extern int SwDeviceCreate(string enumerator,string parent,ref CreateInfo info,uint propertyCount,IntPtr properties,CreationCallback callback,IntPtr context,out IntPtr device);
        [DllImport("swdevice.dll",ExactSpelling=true)]
        private static extern void SwDeviceClose(IntPtr device);

        public bool IsRunning { get { return device!=IntPtr.Zero; } }
        public void Start() {
            if(IsRunning)return;
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
                if(hr<0)throw new Win32Exception(hr,"无法创建虚拟显示设备。请先安装已签名的 IDD 驱动包。");
                if(!state.Done.WaitOne(10000))throw new TimeoutException("等待 Windows 加载虚拟显示驱动超时。");
                if(state.Result<0)throw new Win32Exception(state.Result,"Windows 未能加载虚拟显示驱动。");
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
            if(state!=null){state.Done.Dispose();state=null;}callback=null;
        }
    }
}
