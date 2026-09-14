using System;
using System.IO;
using System.Text;
using System.Net.Sockets;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Web.Script.Serialization;

namespace WiredScreen {
    public sealed class Options {
        public string Source="test",Encoder="auto";
        public int Screen=0,Bitrate=20,Seconds=0,VbvFrames=0;
        public int Adapter=-1;
        public bool PreferGpu=false,GpuFrames=false,Native=false,FreshnessV2=false;
        // V2 is a deliberately constrained experiment: limit encoder
        // look-ahead/VBV pressure and make periodic recovery frames rarer.
        // Keep the stable profile untouched so both can be compared on-device.
        public void UseFreshnessV2(){FreshnessV2=true;Bitrate=12;VbvFrames=4;}
    }
    // Encoded P frames may reference earlier frames. Never silently discard
    // one: fail the session on overload so the next session starts at an IDR.
    public sealed class EncodedFrameQueue {
        private readonly object gate=new object();
        private readonly Queue<EncodedVideoFrame> pending=new Queue<EncodedVideoFrame>();
        private int capacity;
        private bool shrinking;
        private bool completed;
        private Exception failure;
        public EncodedFrameQueue(int capacity=4){if(capacity<1)throw new ArgumentOutOfRangeException("capacity");this.capacity=capacity;}
        public void Publish(byte[] frame) { Publish(new EncodedVideoFrame{Data=frame}); }
        public void Publish(EncodedVideoFrame frame) {
            frame.PacketReadyQpc=Stopwatch.GetTimestamp();
            lock(gate) {
                if(completed)return;
                Stopwatch wait=Stopwatch.StartNew();
                while(pending.Count>=capacity&&!completed){
                    if(shrinking){Monitor.Wait(gate);continue;}
                    int remaining=100-(int)wait.ElapsedMilliseconds;
                    if(remaining<=0)break;
                    Monitor.Wait(gate,remaining);
                }
                if(completed)return;
                if(pending.Count>=capacity&&!shrinking){
                    failure=new IOException("编码帧队列过载，已停止会话以避免损坏参考帧。请重新开始。");
                    pending.Clear();completed=true;Monitor.PulseAll(gate);throw failure;
                }
                pending.Enqueue(frame);
                Monitor.Pulse(gate);
            }
        }
        public bool Take(out byte[] frame) { EncodedVideoFrame video;bool available=Take(out video);frame=available?video.Data:null;return available; }
        public bool Take(out EncodedVideoFrame frame) {
            lock(gate) {
                while(pending.Count==0&&!completed)Monitor.Wait(gate);
                if(failure!=null)throw failure;
                if(pending.Count==0){frame=null;return false;}
                frame=pending.Dequeue();if(pending.Count<capacity)shrinking=false;Monitor.PulseAll(gate);return true;
            }
        }
        public void Complete(Exception error=null) { lock(gate){if(error!=null){failure=error;pending.Clear();}completed=true;Monitor.PulseAll(gate);} }
        public void SetCapacity(int value) { if(value<1)throw new ArgumentOutOfRangeException("value");lock(gate){capacity=value;shrinking=pending.Count>=capacity;Monitor.PulseAll(gate);} }
    }
    public sealed class Engine : IDisposable {
        private readonly string root=AppDomain.CurrentDomain.BaseDirectory;
        public Action<string> Log=Console.WriteLine;
        private volatile bool stopped=false;
        private Process encoder;
        private TcpClient client;
        private int port=0;
        private readonly object gate=new object();
        private string stderr="";
        private long sent=0;
        private double rtt=0;
        private StreamWriter report;
        private EncodedFrameQueue frames;
        public static string Command(string file,string args,int timeout){
            ProcessStartInfo info=new ProcessStartInfo(file,args){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8};
            using(Process p=Process.Start(info)){
                Task<string> output=p.StandardOutput.ReadToEndAsync(),error=p.StandardError.ReadToEndAsync();
                if(!p.WaitForExit(timeout)){try{p.Kill();}catch{}throw new IOException("Command timed out: "+Path.GetFileName(file));}
                Task.WaitAll(output,error);
                if(p.ExitCode!=0)throw new IOException((error.Result+" "+output.Result).Trim());
                return output.Result.Trim();
            }
        }
        private string Adb(string args){return Command(Path.Combine(root,"adb.exe"),"-d "+args,15000);}
        public void Install(){
            string executable=Path.GetFileNameWithoutExtension(Process.GetCurrentProcess().MainModule.FileName);
            string apk=Path.Combine(root,executable+".apk");
            if(!File.Exists(apk))apk=Path.Combine(root,"WiredScreen.apk");
            Log("正在安装 APK；请留意手机安装确认。");Adb("install -r \""+apk+"\"");Log("APK 安装完成。");
        }
        private string ChooseEncoder(Options options){
            string requested=options.Encoder;
            options.GpuFrames=false;
            if(options.PreferGpu&&options.Source=="desktop"&&requested=="h264_nvenc_direct"){
                options.GpuFrames=true;
                try {
                    string probe=Arguments(options,"h264_nvenc").Replace("-f avi pipe:1","-frames:v 3 -f null -");
                    Command(Path.Combine(root,"ffmpeg.exe"),probe,15000);
                    Log("已验证 D3D11 纹理 → CUDA 直接映射 → NVENC，无 CPU 像素往返。");
                    return "h264_nvenc";
                } catch(Exception ex){options.GpuFrames=false;throw new IOException("D3D11 → CUDA → NVENC 直通不可用："+ex.Message,ex);}
            }
            if(options.PreferGpu&&options.Source=="desktop"&&(requested=="auto"||requested=="h264_qsv")){
                options.GpuFrames=true;
                try {
                    string probe=Arguments(options,"h264_qsv").Replace("-f avi pipe:1","-frames:v 3 -f null -");
                    Command(Path.Combine(root,"ffmpeg.exe"),probe,15000);
                    Log("已验证 GPU 纹理映射 → QSV 色彩转换与编码，无显式 CPU 像素下载。");
                    return "h264_qsv";
                } catch(Exception ex){
                    options.GpuFrames=false;
                    Log("GPU 纹理路径不可用，使用兼容捕获："+ex.Message);
                }
            }
            if(requested!="auto")return requested;
            foreach(string name in new[]{"h264_nvenc","h264_qsv","h264_amf"}){
                try{Command(Path.Combine(root,"ffmpeg.exe"),"-hide_banner -loglevel error -f lavfi -i color=size=1920x1080:rate=60 -frames:v 1 -c:v "+name+" -f null -",20000);Log("可用硬件编码器："+name);return name;}catch{ }
            }
            Log("未找到可用硬件编码器，使用软件编码；60 fps 需实测。");return "libx264";
        }
        public static string Arguments(Options options,string codec){
            if(options.VbvFrames<0||options.VbvFrames>4)throw new ArgumentOutOfRangeException("VbvFrames");
            // ddagrab may return repeated desktop samples faster than its
            // declared frame rate. Apply the same source-clock pacing used by
            // testsrc so ADB never receives a burst larger than 60 Hz.
            string input=options.Source=="test"?"-re -f lavfi -i testsrc2=size=1920x1080:rate=60":"-re -f lavfi -i ddagrab=output_idx="+options.Screen+":framerate=60";
            string filter=options.Source=="test"?"format=yuv420p":"hwdownload,format=bgra,scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,format=yuv420p";
            if(options.GpuFrames){
                if(options.Source!="desktop")throw new ArgumentException("GPU texture path requires desktop capture");
                if(codec=="h264_qsv")filter="hwmap=derive_device=qsv,vpp_qsv=w=1920:h=1080:format=nv12";
                else if(codec=="h264_nvenc")filter="hwmap=derive_device=cuda:mode=direct,scale_cuda=w=1920:h=1080:format=nv12";
                else throw new ArgumentException("GPU texture path requires QSV or NVENC");
            }
            string tuning=codec=="h264_nvenc"?"-preset p1 -tune ull -rc cbr -zerolatency 1 -rc-lookahead 0":codec=="h264_qsv"?"-preset veryfast -look_ahead 0 -async_depth 1":codec=="h264_amf"?"-usage ultralowlatency -quality speed":"-preset ultrafast -tune zerolatency -x264-params repeat-headers=1:scenecut=0";
            string vbv=options.VbvFrames>0?" -bufsize "+((options.Bitrate*1000000L*options.VbvFrames+59)/60):" -bufsize "+options.Bitrate+"M";
            string device=options.Adapter>0&&!(options.GpuFrames&&codec=="h264_nvenc")?"-init_hw_device d3d11va=cap:"+options.Adapter+" -filter_hw_device cap ":"";
            int gop=options.FreshnessV2?120:60;
            return "-hide_banner -loglevel warning -nostdin "+device+input+" -an -vf \""+filter+"\" -c:v "+codec+" "+tuning+" -flags low_delay -threads 1 -b:v "+options.Bitrate+"M -maxrate "+options.Bitrate+"M"+vbv+" -g "+gop+" -bf 0 -r 60 -bsf:v h264_metadata=aud=insert -flush_packets 1 -f avi pipe:1";
        }
        public void Run(Options options){
            if(Adb("get-state")!="device")throw new IOException("没有已授权的 USB 设备");
            Log("已确认 USB 设备："+Adb("shell getprop ro.product.model"));
            if(options.Native&&(options.Source!="desktop"||options.Bitrate!=20||options.VbvFrames!=0))throw new ArgumentException("原生模式目前需要虚拟桌面、20 Mbps 和默认 VBV 参数。");
            string codec=options.Native?"native-mf":ChooseEncoder(options);if(stopped)return;
            string session=Guid.NewGuid().ToString("N");
            Adb("shell am start -n com.wiredscreen.usb/.MainActivity --es session "+session);
            port=int.Parse(Adb("forward tcp:0 localabstract:wiredscreen_"+session));
            NetworkStream network=null;
            for(int n=0;n<30&&!stopped;n++){
                try{
                    client=new TcpClient();client.NoDelay=true;client.SendBufferSize=128*1024;client.ReceiveBufferSize=16*1024;
                    client.Connect("127.0.0.1",port);NetworkStream attempt=client.GetStream();attempt.ReadTimeout=1000;
                    if(Encoding.ASCII.GetString(Wire.Exact(attempt,8))!="WREADY01")throw new IOException("Receiver handshake mismatch");
                    network=attempt;break;
                }catch{if(client!=null)client.Close();Thread.Sleep(200);}
            }
            if(network==null)throw new IOException("手机接收端未就绪，请解锁手机并保持 App 在前台");
            network.WriteTimeout=2000;network.ReadTimeout=10000;Wire.Hello(network);
            if(Encoding.ASCII.GetString(Wire.Exact(network,8))!="WCODEC01")throw new IOException("手机解码器初始化失败或 V2 协议不匹配");
            // Tab S4 accepts the codec configuration before it schedules the
            // LocalSocket reader at full speed after a foreground launch.
            // Do not fill ADB's buffers during that one-time transition.
            Thread.Sleep(2500);
            string logs=Path.Combine(root,"logs");Directory.CreateDirectory(logs);
            string reportPath=Path.Combine(logs,"usb-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".jsonl");
            report=new StreamWriter(reportPath,false,Encoding.UTF8){AutoFlush=true};
            report.WriteLine(new JavaScriptSerializer().Serialize(new{type="session",schemaVersion=3,streamProfile=options.FreshnessV2?"freshness-v2":"stable-v1",transport="adb-usb-localabstract",source=options.Source,encoder=codec,pixelPath=options.Native?"idd-d3d11-mf":options.GpuFrames&&codec=="h264_nvenc"?"d3d11-cuda-nvenc":options.GpuFrames?"d3d11-qsv":"cpu-compatible",width=1920,height=1080,targetFps=60,gop=options.FreshnessV2?120:60,vbvFrames=options.VbvFrames,bitrateMbps=options.Bitrate,adapter=options.Adapter,output=options.Screen,encoderArguments=options.Native?"--stream-driver "+options.Seconds:Arguments(options,codec)}));
            Log("USB 视频通道已连接。"+(options.FreshnessV2?"低延迟 V2":"稳定 V1")+" · 编码器 "+codec+"，目标 1920×1080 / 60 fps。");Log("测量记录："+reportPath);
            // Some Android builds do not read their LocalSocket until the
            // first codec output is configured. V2 absorbs that one-time
            // startup burst, then returns to the normal four-frame bound.
            frames=new EncodedFrameQueue(options.FreshnessV2?256:4);
            ProcessStartInfo info=new ProcessStartInfo(Path.Combine(root,options.Native?"GpuHandoffProbe.exe":"ffmpeg.exe"),options.Native?"--stream-driver "+options.Seconds:Arguments(options,codec)){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardErrorEncoding=Encoding.UTF8};
            encoder=Process.Start(info);encoder.ErrorDataReceived+=(s,e)=>{if(e.Data!=null){lock(gate){stderr=(stderr+e.Data+"\n");if(stderr.Length>4000)stderr=stderr.Substring(stderr.Length-4000);}}};encoder.BeginErrorReadLine();
            Task reader=Task.Run(()=>{
                try{
                    if(options.Native){
                        NativePacketReader packets=new NativePacketReader(encoder.StandardOutput.BaseStream);EncodedVideoFrame packet;
                        while(!stopped&&packets.Read(out packet)){if(packet.Frequency!=Stopwatch.Frequency)throw new IOException("Native clock frequency mismatch");frames.Publish(packet);}
                    } else {
                        EncodedPacketReader packets=new EncodedPacketReader(encoder.StandardOutput.BaseStream);byte[] packet;
                        while(!stopped&&packets.Read(out packet))frames.Publish(packet);
                    }
                }catch(Exception ex){if(!stopped)frames.Complete(ex);}finally{frames.Complete();}
            });
            Task telemetry=Task.Run(()=>{
                try{while(!stopped){
                    Packet packet=Wire.Read(network);
                    if(packet.Kind==3)rtt=(Stopwatch.GetTimestamp()-packet.Stamp)*1000.0/Stopwatch.Frequency;
                    else if(packet.Kind==5){
                        if(packet.Data.Length!=16)throw new InvalidDataException("Invalid render acknowledgement");
                        double confirmed=(Stopwatch.GetTimestamp()-packet.Stamp)*1000.0/Stopwatch.Frequency;
                        double receiver=BitConverter.ToInt64(packet.Data,0)/1e6;
                        double callbackLag=BitConverter.ToInt64(packet.Data,8)/1e6;
                        // The reported render timestamp may use an incompatible
                        // device clock. The sum cancels it and measures only
                        // receiver-local arrival to callback handling.
                        lock(gate){if(report!=null)report.WriteLine(new JavaScriptSerializer().Serialize(new{type="frame",schemaVersion=2,sequence=packet.Sequence,sendToRenderAckMs=confirmed,receiveToCallbackMs=receiver+callbackLag,renderTimestampValid=receiver>=0&&callbackLag>=0,receiveToRenderMs=receiver,renderCallbackLagMs=callbackLag}));}
                        if(options.FreshnessV2&&packet.Sequence==0&&frames!=null){frames.SetCapacity(4);Log("V2 首帧已呈现；传输队列恢复为 4 帧上限。");}
                    }
                    else if(packet.Kind==4){
                        string json=Encoding.UTF8.GetString(packet.Data);Dictionary<string,object> stats=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(json);
                        stats["pcTimeUtc"]=DateTime.UtcNow.ToString("o");stats["usbRoundTripMs"]=rtt;stats["sentFrames"]=Interlocked.Read(ref sent);
                        lock(gate){if(report!=null)report.WriteLine(new JavaScriptSerializer().Serialize(stats));}
                        Log(String.Format("接收解码 {0:F1} / 呈现回调 {1:F1} fps · USB 往返 {2:F1} ms",Convert.ToDouble(stats["decodedFps"]),Convert.ToDouble(stats["renderedFps"]),rtt));
                    } else throw new InvalidDataException("Unknown receiver packet");
                }}catch(Exception ex){if(!stopped){Log("接收统计中断："+ex.Message);Stop();}}
            });
            Stopwatch elapsed=Stopwatch.StartNew();int sequence=0;long pacingStart=0;
            try{
                EncodedVideoFrame frame;
                while(frames.Take(out frame)){
                    if(stopped)break;
                    // Capture APIs can release duplicate desktop frames in a
                    // burst. Pace packets at the declared display rate so a
                    // USB buffer cannot turn that burst into display latency.
                    if(sequence==0)pacingStart=Stopwatch.GetTimestamp();
                    // The native D3D11/MF path has one frame in flight and
                    // therefore provides its own backpressure. Applying a
                    // second nominal-60Hz scheduler here accumulates delay
                    // whenever the desktop source jitters.
                    long due=options.Native?frame.PacketReadyQpc:pacingStart+(long)sequence*Stopwatch.Frequency/60;
                    if(!options.Native)while(!stopped){long remaining=due-Stopwatch.GetTimestamp();if(remaining<=0)break;int wait=(int)(remaining*1000/Stopwatch.Frequency);if(wait>0)Thread.Sleep(Math.Min(wait,10));else Thread.SpinWait(64);}
                    long sendQpc=Stopwatch.GetTimestamp();
                    Wire.Write(network,1,sequence,sendQpc,frame.Data);Interlocked.Increment(ref sent);
                    long writeDoneQpc=Stopwatch.GetTimestamp();
                    lock(gate){if(report!=null)report.WriteLine(new JavaScriptSerializer().Serialize(new{type="send",sequence=sequence,packetBytes=frame.Data.Length,packetReadyQpc=frame.PacketReadyQpc,sendQpc=sendQpc,writeDoneQpc=writeDoneQpc,targetSendQpc=due,qpcFrequency=Stopwatch.Frequency,scheduleErrorMs=(sendQpc-due)*1000.0/Stopwatch.Frequency,readyToSendMs=(sendQpc-frame.PacketReadyQpc)*1000.0/Stopwatch.Frequency,writeMs=(writeDoneQpc-sendQpc)*1000.0/Stopwatch.Frequency}));}
                    if(options.Native){lock(gate){if(report!=null)report.WriteLine(new JavaScriptSerializer().Serialize(new{type="native-frame",sequence=sequence,sourceSequence=frame.SourceSequence,capturedQpc=frame.CapturedQpc,encodedQpc=frame.EncodedQpc,sendQpc=sendQpc,qpcFrequency=frame.Frequency,captureToEncodedMs=(frame.EncodedQpc-frame.CapturedQpc)*1000.0/frame.Frequency,encodedToSendMs=(sendQpc-frame.EncodedQpc)*1000.0/frame.Frequency}));}}
                    if(sequence%60==0)Wire.Write(network,2,sequence,Stopwatch.GetTimestamp(),new byte[0]);sequence++;
                    if(options.Seconds>0&&elapsed.Elapsed.TotalSeconds>=options.Seconds)break;
                }
                if(reader.IsFaulted)throw reader.Exception.GetBaseException();
                if(!stopped&&encoder.WaitForExit(2000)&&encoder.ExitCode!=0)throw new IOException("编码进程失败："+stderr);
                if(!stopped&&sequence<2)throw new IOException("编码没有生成画面："+stderr);
            }finally{
                Stop();try{Task.WaitAll(new[]{reader,telemetry},5000);}catch{}
                lock(gate){if(report!=null){report.Dispose();report=null;}}
                Log("传输已停止，已发送 "+sent+" 帧；编码参考帧按顺序发送，过载时终止会话。");
                frames=null;
            }
        }
        public void Stop(){
            stopped=true;
            if(frames!=null)frames.Complete();
            try{if(client!=null)client.Close();}catch{}
            try{if(encoder!=null&&!encoder.HasExited)encoder.Kill();}catch{}
        }
        public void Dispose(){Stop();if(port!=0){try{Adb("forward --remove tcp:"+port);}catch{}port=0;}if(encoder!=null)encoder.Dispose();}
    }
}
