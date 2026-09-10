using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace WiredScreen {
    public class MainWindow:Form {
        private readonly TextBox log=new TextBox();
        private readonly ComboBox source=new ComboBox(),codec=new ComboBox();
        private readonly NumericUpDown screen=new NumericUpDown();
        private readonly Button start=new Button(),stop=new Button(),install=new Button(),virtualStart=new Button(),virtualStop=new Button();
        private Engine engine;
        private VirtualDisplayController virtualDisplay;
        public MainWindow(){
            Text="USB 副屏实验室 · 原生版";Width=900;Height=650;MinimumSize=new Size(700,500);Font=new Font("Microsoft YaHei UI",10);BackColor=Color.FromArgb(15,23,35);ForeColor=Color.White;
            TableLayoutPanel layout=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(24),RowCount=4,ColumnCount=1};
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,85));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,75));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,60));layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));Controls.Add(layout);
            Label title=new Label{Text="USB 直连，不需要网络共享。\n目标 1920×1080 / 60 fps · 可通过已安装的 IDD 驱动注册 Windows 虚拟扩展屏。",Dock=DockStyle.Fill,AutoSize=false,Font=new Font(Font.FontFamily,13)};layout.Controls.Add(title,0,0);
            FlowLayoutPanel choices=new FlowLayoutPanel{Dock=DockStyle.Fill};source.DropDownStyle=ComboBoxStyle.DropDownList;source.Width=170;source.Items.AddRange(new object[]{"动态测试画面","现有桌面画面"});source.SelectedIndex=0;
            codec.DropDownStyle=ComboBoxStyle.DropDownList;codec.Width=160;codec.Items.AddRange(new object[]{"auto","h264_nvenc","h264_qsv","h264_amf","libx264"});codec.SelectedIndex=0;
            screen.Maximum=8;screen.Width=60;
            choices.Controls.Add(source);choices.Controls.Add(codec);choices.Controls.Add(new Label{Text="屏幕索引",AutoSize=true,Padding=new Padding(0,5,0,0)});choices.Controls.Add(screen);layout.Controls.Add(choices,0,1);
            FlowLayoutPanel buttons=new FlowLayoutPanel{Dock=DockStyle.Fill};install.Text="安装手机 App";virtualStart.Text="注册虚拟副屏";virtualStop.Text="移除虚拟副屏";start.Text="开始 USB 投屏";stop.Text="停止";stop.Enabled=false;virtualStop.Enabled=false;
            foreach(Button b in new[]{install,virtualStart,virtualStop,start,stop}){b.Width=150;b.Height=38;b.BackColor=Color.FromArgb(145,229,194);b.ForeColor=Color.FromArgb(10,30,25);buttons.Controls.Add(b);}layout.Controls.Add(buttons,0,2);
            log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Dock=DockStyle.Fill;log.BackColor=Color.FromArgb(8,15,25);log.ForeColor=Color.FromArgb(190,218,220);layout.Controls.Add(log,0,3);
            install.Click+=async(s,e)=>{install.Enabled=false;try{using(Engine x=new Engine()){x.Log=Write;await Task.Run(()=>x.Install());}}catch(Exception ex){Write(ex.Message);}finally{install.Enabled=true;}};
            virtualStart.Click+=(s,e)=>{try{virtualDisplay=new VirtualDisplayController();VirtualDisplayTarget target=virtualDisplay.Start();DisplayTopology.ExtendDesktop();source.SelectedIndex=1;virtualStart.Enabled=false;virtualStop.Enabled=true;Write("Windows 已枚举 "+target.DeviceString+"（"+target.DeviceName+"）。已请求扩展桌面；请核对“显示设置”，再选择可用的屏幕索引开始投屏。");}catch(Exception ex){if(virtualDisplay!=null){virtualDisplay.Dispose();virtualDisplay=null;}Write("虚拟副屏未启动："+ex.Message+" 需先用 native/scripts/build_idd.ps1 构建并安装 WDK 签名的驱动包。");}};
            virtualStop.Click+=(s,e)=>{if(virtualDisplay!=null){virtualDisplay.Dispose();virtualDisplay=null;}virtualStart.Enabled=true;virtualStop.Enabled=false;Write("虚拟显示器已移除。");};
            start.Click+=async(s,e)=>{
                start.Enabled=false;install.Enabled=false;stop.Enabled=true;
                Options o=new Options{Source=source.SelectedIndex==0?"test":"desktop",Encoder=codec.Text,Screen=(int)screen.Value};
                engine=new Engine{Log=Write};try{await Task.Run(()=>{if(o.Source=="desktop"&&virtualDisplay!=null&&virtualDisplay.IsRunning){VirtualDisplayTarget t=DisplayTopology.WaitForSampleDisplay(10000);uint a,b;if(t==null||DisplayTopology.WiredScreenFindOutput(t.DeviceName,out a,out b)<0)throw new Exception("虚拟屏尚未就绪，请稍后重试。");o.Adapter=(int)a;o.Screen=(int)b;}engine.Run(o);});}catch(Exception ex){Write("错误："+ex.Message);}finally{engine.Dispose();engine=null;start.Enabled=true;install.Enabled=true;stop.Enabled=false;}
            };
            stop.Click+=(s,e)=>{if(engine!=null)engine.Stop();};
            FormClosing+=(s,e)=>{if(engine!=null)engine.Stop();if(virtualDisplay!=null)virtualDisplay.Dispose();};
            Write("手机开启 USB 调试并授权电脑。首次使用先安装 App，再开始测试；虚拟副屏需要先构建并安装 IDD 驱动。");
        }
        private void Write(string text){if(IsDisposed||!IsHandleCreated)return;BeginInvoke((Action)(()=>{if(!IsDisposed){log.AppendText(DateTime.Now.ToString("HH:mm:ss")+" "+text+Environment.NewLine);if(log.TextLength>20000)log.Text=log.Text.Substring(log.TextLength-15000);}}));}
    }
    public static class Program {
        [STAThread] public static int Main(string[] args){
            try{
                if(Array.IndexOf(args,"--pattern")>=0){
                    int at=Array.IndexOf(args,"--display");
                    if(at<0||at+1>=args.Length)throw new ArgumentException("--display is required");
                    Application.EnableVisualStyles();Application.Run(new LatencyPattern(args[at+1]));return 0;
                }
                if(Array.IndexOf(args,"--self-test")>=0){ProtocolTests.Run();return 0;}
                if(Array.IndexOf(args,"--install")>=0){using(Engine engine=new Engine())engine.Install();return 0;}
                if(Array.IndexOf(args,"--virtual")>=0){
                    int seconds=30,vbvFrames=0;
                    for(int i=0;i<args.Length-1;i++){if(args[i]=="--seconds")seconds=int.Parse(args[i+1]);if(args[i]=="--vbv-frames")vbvFrames=int.Parse(args[i+1]);}
                    if(vbvFrames<0||vbvFrames>4)throw new ArgumentOutOfRangeException("--vbv-frames");
                    using(VirtualDisplayController display=new VirtualDisplayController()) {
                        VirtualDisplayTarget target=display.Start();
                        System.Threading.Thread.Sleep(10000);
                        DisplayTopology.ExtendDesktop();
                        System.Threading.Thread.Sleep(2000);
                        uint adapter,output;
                        int hr=DisplayTopology.WiredScreenFindOutput(target.DeviceName,out adapter,out output);
                        if(hr<0)throw new Exception("虚拟屏没有可用的 DXGI 捕获输出：0x"+hr.ToString("X8"));
                        Console.WriteLine("虚拟屏 "+target.DeviceName+" adapter="+adapter+" output="+output);
                        System.Diagnostics.Process pattern=null;
                        try {
                            if(Array.IndexOf(args,"--dynamic")>=0)pattern=System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(Application.ExecutablePath,"--pattern --display "+target.DeviceName){UseShellExecute=false});
                            using(Engine engine=new Engine())engine.Run(new Options{Source="desktop",Adapter=(int)adapter,Screen=(int)output,Seconds=seconds,VbvFrames=vbvFrames});
                        } finally {if(pattern!=null){if(!pattern.HasExited){pattern.CloseMainWindow();if(!pattern.WaitForExit(2000))pattern.Kill();}pattern.Dispose();}}
                    }
                    return 0;
                }
                if(Array.IndexOf(args,"--test")>=0||Array.IndexOf(args,"--desktop")>=0){
                    Options options=new Options{Source=Array.IndexOf(args,"--desktop")>=0?"desktop":"test",Seconds=30};
                    for(int i=0;i<args.Length-1;i++){if(args[i]=="--seconds")options.Seconds=int.Parse(args[i+1]);if(args[i]=="--encoder")options.Encoder=args[i+1];if(args[i]=="--screen")options.Screen=int.Parse(args[i+1]);if(args[i]=="--vbv-frames")options.VbvFrames=int.Parse(args[i+1]);}
                    using(Engine engine=new Engine()){Console.CancelKeyPress+=(s,e)=>{e.Cancel=true;engine.Stop();};engine.Run(options);}return 0;
                }
                Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new MainWindow());return 0;
            }catch(Exception ex){Console.Error.WriteLine(ex.Message);return 1;}
        }
    }
}
