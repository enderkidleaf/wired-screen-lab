using System;
using System.Drawing;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace WiredScreen {
    public sealed class StreamPreset {
        public readonly int Width,Height;
        public StreamPreset(int width,int height){Width=width;Height=height;}
        public override string ToString(){return Width+" × "+Height;}
    }
    public class MainWindow:Form {
        private readonly TextBox log=new TextBox(),contentPath=new TextBox();
        private readonly ComboBox mode=new ComboBox(),codec=new ComboBox(),profile=new ComboBox(),resolution=new ComboBox(),frameRate=new ComboBox();
        private readonly NumericUpDown screen=new NumericUpDown(),bitrate=new NumericUpDown();
        private readonly Button start=new Button(),stop=new Button(),install=new Button(),prepare=new Button(),virtualStop=new Button(),browse=new Button();
        private readonly Label contentLabel=new Label(),sourceLabel=new Label();
        private Engine engine;
        private VirtualDisplayController virtualDisplay;
        private bool creatingDisplay;
        private int preparedAdapter=-1,preparedOutput=-1,preparedMode=-1;

        public MainWindow(int initialMode,bool autoStart){
            Text="USB 副屏实验室 · 原生版";Width=960;Height=720;MinimumSize=new Size(760,560);Font=new Font("Microsoft YaHei UI",10);BackColor=Color.FromArgb(15,23,35);ForeColor=Color.White;
            TableLayoutPanel layout=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(24),RowCount=6,ColumnCount=1};
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,72));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,52));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,48));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,54));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,52));layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));Controls.Add(layout);
            Label title=new Label{Text="USB 直连副屏\n扩展、复制和视频播放均通过同一条 USB 视频链路；分辨率与帧率会实际传给编码器和手机端。",Dock=DockStyle.Fill,AutoSize=false,Font=new Font(Font.FontFamily,12)};layout.Controls.Add(title,0,0);

            FlowLayoutPanel modes=new FlowLayoutPanel{Dock=DockStyle.Fill,WrapContents=false};
            mode.DropDownStyle=ComboBoxStyle.DropDownList;mode.Width=230;mode.Items.AddRange(new object[]{"扩展桌面（虚拟副屏）","复制桌面（镜像显示）","播放指定视频文件","动态测试画面"});mode.SelectedIndex=initialMode>=0&&initialMode<4?initialMode:0;
            sourceLabel.AutoSize=true;sourceLabel.Padding=new Padding(12,7,0,0);modes.Controls.Add(new Label{Text="用途",AutoSize=true,Padding=new Padding(0,7,0,0)});modes.Controls.Add(mode);modes.Controls.Add(sourceLabel);layout.Controls.Add(modes,0,1);

            FlowLayoutPanel content=new FlowLayoutPanel{Dock=DockStyle.Fill,WrapContents=false};contentLabel.Text="播放文件";contentLabel.AutoSize=true;contentLabel.Padding=new Padding(0,7,0,0);contentPath.ReadOnly=true;contentPath.Width=490;browse.Text="选择视频";browse.Width=105;content.Controls.Add(contentLabel);content.Controls.Add(contentPath);content.Controls.Add(browse);layout.Controls.Add(content,0,2);

            FlowLayoutPanel settings=new FlowLayoutPanel{Dock=DockStyle.Fill,WrapContents=false};
            resolution.DropDownStyle=ComboBoxStyle.DropDownList;resolution.Width=120;resolution.Items.AddRange(new object[]{new StreamPreset(1920,1080),new StreamPreset(1600,900),new StreamPreset(1280,720)});resolution.SelectedIndex=0;
            frameRate.DropDownStyle=ComboBoxStyle.DropDownList;frameRate.Width=76;frameRate.Items.AddRange(new object[]{"60 fps","50 fps","30 fps"});frameRate.SelectedIndex=0;
            bitrate.Minimum=2;bitrate.Maximum=60;bitrate.Value=12;bitrate.Width=58;
            codec.DropDownStyle=ComboBoxStyle.DropDownList;codec.Width=155;codec.Items.AddRange(new object[]{"auto","h264_nvenc_direct","h264_nvenc","h264_qsv","h264_amf","libx264","native-mf"});codec.SelectedIndex=0;
            profile.DropDownStyle=ComboBoxStyle.DropDownList;profile.Width=145;profile.Items.AddRange(new object[]{"低延迟 V2（推荐）","稳定兼容 V1"});profile.SelectedIndex=0;
            screen.Minimum=0;screen.Maximum=8;screen.Width=52;
            settings.Controls.Add(new Label{Text="分辨率",AutoSize=true,Padding=new Padding(0,7,0,0)});settings.Controls.Add(resolution);
            settings.Controls.Add(new Label{Text="帧率",AutoSize=true,Padding=new Padding(10,7,0,0)});settings.Controls.Add(frameRate);
            settings.Controls.Add(new Label{Text="码率",AutoSize=true,Padding=new Padding(10,7,0,0)});settings.Controls.Add(bitrate);settings.Controls.Add(new Label{Text="Mbps",AutoSize=true,Padding=new Padding(2,7,0,0)});
            settings.Controls.Add(new Label{Text="编码器",AutoSize=true,Padding=new Padding(10,7,0,0)});settings.Controls.Add(codec);settings.Controls.Add(profile);layout.Controls.Add(settings,0,3);

            FlowLayoutPanel buttons=new FlowLayoutPanel{Dock=DockStyle.Fill,WrapContents=false};install.Text="安装手机 App";prepare.Text="创建并应用副屏";virtualStop.Text="移除虚拟副屏";start.Text="开始传输";stop.Text="停止";stop.Enabled=false;virtualStop.Enabled=false;
            foreach(Button button in new[]{install,prepare,virtualStop,start,stop}){button.Width=150;button.Height=38;button.BackColor=Color.FromArgb(145,229,194);button.ForeColor=Color.FromArgb(10,30,25);buttons.Controls.Add(button);}layout.Controls.Add(buttons,0,4);
            log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Dock=DockStyle.Fill;log.BackColor=Color.FromArgb(8,15,25);log.ForeColor=Color.FromArgb(190,218,220);layout.Controls.Add(log,0,5);

            mode.SelectedIndexChanged+=(s,e)=>{preparedMode=-1;RefreshModeUi();};browse.Click+=(s,e)=>ChooseContent();
            install.Click+=async(s,e)=>{install.Enabled=false;try{using(Engine x=new Engine()){x.Log=Write;await Task.Run(()=>x.Install());}}catch(Exception ex){Write("安装失败："+ex.Message);}finally{install.Enabled=true;}};
            prepare.Click+=async(s,e)=>await PrepareVirtualDisplay(false);
            virtualStop.Click+=(s,e)=>RemoveVirtualDisplay();
            start.Click+=async(s,e)=>await StartTransmission();
            stop.Click+=(s,e)=>{if(engine!=null)engine.Stop();};
            FormClosing+=(s,e)=>{if(creatingDisplay){e.Cancel=true;Write("正在创建副屏，请等待完成后关闭。");return;}if(engine!=null)engine.Stop();RemoveVirtualDisplay();};
            RefreshModeUi();
            Write("手机开启 USB 调试并授权电脑。扩展和复制模式会创建 Windows 虚拟显示器；播放文件只向手机发送选定视频，不传输音频。");
            if(autoStart)Shown+=async(s,e)=>{await PrepareVirtualDisplay(true);};
        }

        private bool NeedsVirtualDisplay(){return mode.SelectedIndex==0||mode.SelectedIndex==1;}
        private bool IsClone(){return mode.SelectedIndex==1;}
        private void RefreshModeUi(){
            bool file=mode.SelectedIndex==2,desktop=NeedsVirtualDisplay();contentLabel.Visible=file;contentPath.Visible=file;browse.Visible=file;
            sourceLabel.Text=file?"视频会循环播放到手机（不含声音）":desktop?(IsClone()?"Windows 将复制现有桌面到虚拟显示器":"Windows 将创建独立的扩展桌面"):"生成本机测试画面";
            prepare.Enabled=desktop&&engine==null;virtualStop.Enabled=virtualDisplay!=null&&engine==null;
        }
        private void ChooseContent(){using(OpenFileDialog dialog=new OpenFileDialog{Title="选择要发送到手机的视频文件",Filter="视频文件|*.mp4;*.mkv;*.mov;*.avi;*.webm;*.m4v|所有文件|*.*",CheckFileExists=true})if(dialog.ShowDialog(this)==DialogResult.OK){contentPath.Text=dialog.FileName;Write("已选择内容："+Path.GetFileName(dialog.FileName));}}
        private StreamPreset SelectedPreset(){return (StreamPreset)resolution.SelectedItem;}
        private int SelectedFrameRate(){return Int32.Parse(frameRate.Text.Split(' ')[0]);}
        private Options CreateOptions(){
            StreamPreset preset=SelectedPreset();string source=mode.SelectedIndex==2?"file":mode.SelectedIndex==3?"test":"desktop";
            Options options=new Options{Source=source,ContentPath=contentPath.Text,Encoder=codec.Text,Native=codec.Text=="native-mf",Screen=(int)screen.Value,Width=preset.Width,Height=preset.Height,FrameRate=SelectedFrameRate(),Bitrate=(int)bitrate.Value};
            if(profile.SelectedIndex==0){options.UseFreshnessV2();options.Bitrate=(int)bitrate.Value;}
            return options;
        }
        private static bool IsAdministrator(){return new System.Security.Principal.WindowsPrincipal(System.Security.Principal.WindowsIdentity.GetCurrent()).IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);}
        private async Task PrepareVirtualDisplay(bool autoStart){
            if(!NeedsVirtualDisplay()){Write("当前用途不需要创建 Windows 虚拟副屏。");return;}
            if(!IsAdministrator()){
                try{System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(Application.ExecutablePath,"--ui-mode "+mode.SelectedIndex+(autoStart?" --ui-start":"")){UseShellExecute=true,Verb="runas",WorkingDirectory=AppDomain.CurrentDomain.BaseDirectory});Close();}
                catch(System.ComponentModel.Win32Exception ex){Write(ex.NativeErrorCode==1223?"已取消管理员授权，未创建副屏。":"无法获取管理员权限："+ex.Message);}return;
            }
            creatingDisplay=true;SetControls(false);bool ready=false;
            try{
                if(virtualDisplay==null){virtualDisplay=new VirtualDisplayController();await Task.Run(()=>virtualDisplay.Start());}
                await Task.Run(()=>{if(IsClone())DisplayTopology.CloneDesktop();else DisplayTopology.ExtendDesktop();});
                VirtualDisplayTarget target=await Task.Run(()=>DisplayTopology.WaitForSampleDisplay(10000));uint adapter,output;
                if(target==null||DisplayTopology.WiredScreenFindOutput(target.DeviceName,out adapter,out output)<0)throw new Exception("虚拟屏已创建，但 DXGI 捕获输出尚未就绪。");
                preparedAdapter=(int)adapter;preparedOutput=(int)output;preparedMode=mode.SelectedIndex;ready=true;
                Write((IsClone()?"已复制桌面到 ":"已创建扩展桌面 ")+target.DeviceName+"。Windows 现在可在显示设置中调整其位置和主副屏关系。");
            }catch(Exception ex){if(virtualDisplay!=null){virtualDisplay.Dispose();virtualDisplay=null;}preparedAdapter=preparedOutput=preparedMode=-1;Write("副屏启动失败："+ex.Message+" 请检查管理员授权及已安装驱动的状态。");}
            finally{creatingDisplay=false;SetControls(true);RefreshModeUi();}
            if(ready&&autoStart)await StartTransmission();
        }
        private async Task StartTransmission(){
            if(NeedsVirtualDisplay()&&(virtualDisplay==null||!virtualDisplay.IsRunning||preparedAdapter<0||preparedMode!=mode.SelectedIndex)){await PrepareVirtualDisplay(true);return;}
            Options options;
            try{options=CreateOptions();options.Validate();if(options.Source=="desktop"&&preparedAdapter>=0){options.Adapter=preparedAdapter;options.Screen=preparedOutput;options.PreferGpu=options.Encoder=="h264_qsv"||options.Encoder=="h264_nvenc_direct";}}
            catch(Exception ex){Write("请检查设置："+ex.Message);return;}
            SetControls(false);stop.Enabled=true;engine=new Engine{Log=Write};
            try{await Task.Run(()=>engine.Run(options));}catch(Exception ex){Write("错误："+ex.Message);}
            finally{engine.Dispose();engine=null;stop.Enabled=false;SetControls(true);RefreshModeUi();}
        }
        private void SetControls(bool enabled){foreach(Control control in new Control[]{mode,codec,profile,resolution,frameRate,bitrate,screen,browse,install,prepare,virtualStop,start})control.Enabled=enabled;}
        private void RemoveVirtualDisplay(){if(virtualDisplay!=null){virtualDisplay.Dispose();virtualDisplay=null;}preparedAdapter=preparedOutput=preparedMode=-1;RefreshModeUi();Write("虚拟显示器已移除。");}
        private void Write(string text){if(IsDisposed||!IsHandleCreated)return;BeginInvoke((Action)(()=>{if(!IsDisposed){log.AppendText(DateTime.Now.ToString("HH:mm:ss")+" "+text+Environment.NewLine);if(log.TextLength>20000)log.Text=log.Text.Substring(log.TextLength-15000);}}));}
    }
    public static class Program {
        [STAThread] public static int Main(string[] args){
            try{
                if(Array.IndexOf(args,"--self-test")>=0){ProtocolTests.Run();return 0;}
                if(Array.IndexOf(args,"--test")>=0||Array.IndexOf(args,"--desktop")>=0){Options options=new Options{Source=Array.IndexOf(args,"--desktop")>=0?"desktop":"test",Seconds=30};if(Array.IndexOf(args,"--v2")>=0)options.UseFreshnessV2();for(int i=0;i<args.Length-1;i++){if(args[i]=="--seconds")options.Seconds=int.Parse(args[i+1]);if(args[i]=="--encoder")options.Encoder=args[i+1];if(args[i]=="--screen")options.Screen=int.Parse(args[i+1]);if(args[i]=="--width")options.Width=int.Parse(args[i+1]);if(args[i]=="--height")options.Height=int.Parse(args[i+1]);if(args[i]=="--fps")options.FrameRate=int.Parse(args[i+1]);}using(Engine engine=new Engine()){Console.CancelKeyPress+=(s,e)=>{e.Cancel=true;engine.Stop();};engine.Run(options);}return 0;}
                int uiMode=-1;for(int i=0;i<args.Length-1;i++)if(args[i]=="--ui-mode")uiMode=int.Parse(args[i+1]);
                Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new MainWindow(uiMode,Array.IndexOf(args,"--ui-start")>=0));return 0;
            }catch(Exception ex){Console.Error.WriteLine(ex.Message);return 1;}
        }
    }
}
