using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace WiredScreen {
    public class MainWindow:Form {
        private readonly TextBox log=new TextBox();
        private readonly ComboBox source=new ComboBox(),codec=new ComboBox();
        private readonly NumericUpDown screen=new NumericUpDown();
        private readonly Button start=new Button(),stop=new Button(),install=new Button();
        private Engine engine;
        public MainWindow(){
            Text="USB 副屏实验室 · 原生版";Width=900;Height=650;MinimumSize=new Size(700,500);Font=new Font("Microsoft YaHei UI",10);BackColor=Color.FromArgb(15,23,35);ForeColor=Color.White;
            TableLayoutPanel layout=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(24),RowCount=4,ColumnCount=1};
            layout.RowStyles.Add(new RowStyle(SizeType.Absolute,85));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,75));layout.RowStyles.Add(new RowStyle(SizeType.Absolute,60));layout.RowStyles.Add(new RowStyle(SizeType.Percent,100));Controls.Add(layout);
            Label title=new Label{Text="USB 直连，不需要网络共享。\n目标 1920×1080 / 60 fps · 当前版本先验证传输，尚未新增扩展屏。",Dock=DockStyle.Fill,AutoSize=false,Font=new Font(Font.FontFamily,13)};layout.Controls.Add(title,0,0);
            FlowLayoutPanel choices=new FlowLayoutPanel{Dock=DockStyle.Fill};source.DropDownStyle=ComboBoxStyle.DropDownList;source.Width=170;source.Items.AddRange(new object[]{"动态测试画面","现有桌面画面"});source.SelectedIndex=0;
            codec.DropDownStyle=ComboBoxStyle.DropDownList;codec.Width=160;codec.Items.AddRange(new object[]{"auto","h264_nvenc","h264_qsv","h264_amf","libx264"});codec.SelectedIndex=0;
            screen.Maximum=8;screen.Width=60;
            choices.Controls.Add(source);choices.Controls.Add(codec);choices.Controls.Add(new Label{Text="屏幕索引",AutoSize=true,Padding=new Padding(0,5,0,0)});choices.Controls.Add(screen);layout.Controls.Add(choices,0,1);
            FlowLayoutPanel buttons=new FlowLayoutPanel{Dock=DockStyle.Fill};install.Text="安装手机 App";start.Text="开始 USB 投屏";stop.Text="停止";stop.Enabled=false;
            foreach(Button b in new[]{install,start,stop}){b.Width=150;b.Height=38;b.BackColor=Color.FromArgb(145,229,194);b.ForeColor=Color.FromArgb(10,30,25);buttons.Controls.Add(b);}layout.Controls.Add(buttons,0,2);
            log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Dock=DockStyle.Fill;log.BackColor=Color.FromArgb(8,15,25);log.ForeColor=Color.FromArgb(190,218,220);layout.Controls.Add(log,0,3);
            install.Click+=async(s,e)=>{install.Enabled=false;try{using(Engine x=new Engine()){x.Log=Write;await Task.Run(()=>x.Install());}}catch(Exception ex){Write(ex.Message);}finally{install.Enabled=true;}};
            start.Click+=async(s,e)=>{
                start.Enabled=false;install.Enabled=false;stop.Enabled=true;
                Options o=new Options{Source=source.SelectedIndex==0?"test":"desktop",Encoder=codec.Text,Screen=(int)screen.Value};
                engine=new Engine{Log=Write};try{await Task.Run(()=>engine.Run(o));}catch(Exception ex){Write("错误："+ex.Message);}finally{engine.Dispose();engine=null;start.Enabled=true;install.Enabled=true;stop.Enabled=false;}
            };
            stop.Click+=(s,e)=>{if(engine!=null)engine.Stop();};
            FormClosing+=(s,e)=>{if(engine!=null)engine.Stop();};
            Write("手机开启 USB 调试并授权电脑。首次使用先安装 App，再开始测试。");
        }
        private void Write(string text){if(IsDisposed||!IsHandleCreated)return;BeginInvoke((Action)(()=>{if(!IsDisposed){log.AppendText(DateTime.Now.ToString("HH:mm:ss")+" "+text+Environment.NewLine);if(log.TextLength>20000)log.Text=log.Text.Substring(log.TextLength-15000);}}));}
    }
    public static class Program {
        [STAThread] public static int Main(string[] args){
            try{
                if(Array.IndexOf(args,"--self-test")>=0){ProtocolTests.Run();return 0;}
                if(Array.IndexOf(args,"--install")>=0){using(Engine engine=new Engine())engine.Install();return 0;}
                if(Array.IndexOf(args,"--test")>=0||Array.IndexOf(args,"--desktop")>=0){
                    Options options=new Options{Source=Array.IndexOf(args,"--desktop")>=0?"desktop":"test",Seconds=30};
                    for(int i=0;i<args.Length-1;i++){if(args[i]=="--seconds")options.Seconds=int.Parse(args[i+1]);if(args[i]=="--encoder")options.Encoder=args[i+1];if(args[i]=="--screen")options.Screen=int.Parse(args[i+1]);}
                    using(Engine engine=new Engine()){Console.CancelKeyPress+=(s,e)=>{e.Cancel=true;engine.Stop();};engine.Run(options);}return 0;
                }
                Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new MainWindow());return 0;
            }catch(Exception ex){Console.Error.WriteLine(ex.Message);return 1;}
        }
    }
}
