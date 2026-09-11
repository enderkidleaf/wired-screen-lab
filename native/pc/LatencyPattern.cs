using System;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;

namespace WiredScreen {
    // Visible source identifiers distinguish new desktop content from repeated
    // encoded frames. They are not yet correlated with protocol frame IDs.
    public sealed class LatencyPattern : Form {
        private readonly Timer timer=new Timer{Interval=16};
        private readonly Stopwatch clock=Stopwatch.StartNew();
        private long frame;
        [DllImport("winmm.dll")] private static extern uint timeBeginPeriod(uint period);
        [DllImport("winmm.dll")] private static extern uint timeEndPeriod(uint period);
        private bool timerPeriod;
        public LatencyPattern(string display) {
            Text="WiredScreen latency pattern";BackColor=Color.Black;ForeColor=Color.White;
            DoubleBuffered=true;FormBorderStyle=FormBorderStyle.None;StartPosition=FormStartPosition.Manual;
            Screen chosen=null;foreach(Screen s in Screen.AllScreens)if(s.DeviceName==display)chosen=s;
            if(chosen==null)throw new ArgumentException("Requested display is not active");
            Bounds=chosen.Bounds;KeyPreview=true;
            timerPeriod=timeBeginPeriod(1)==0;
            KeyDown+=(s,e)=>{if(e.KeyCode==Keys.Escape)Close();else BackColor=BackColor==Color.Black?Color.DarkBlue:Color.Black;};
            timer.Tick+=(s,e)=>{frame++;Invalidate();};timer.Start();
        }
        protected override void OnPaint(PaintEventArgs e) {
            base.OnPaint(e);
            using(Font f=new Font("Consolas",32)) {
                e.Graphics.DrawString("SOURCE FRAME "+frame+"\n"+clock.ElapsedMilliseconds+" ms\nPress a key to change color",f,Brushes.White,40,40);
            }
            int x=(int)((clock.ElapsedMilliseconds*0.7)%Math.Max(1,ClientSize.Width-80));
            e.Graphics.FillRectangle(Brushes.Lime,x,260,80,Math.Max(1,ClientSize.Height-300));
            using(Font f=new Font("Consolas",12))e.Graphics.DrawString("1080p desktop capture | text clarity 0123456789 ABCDEFG",f,Brushes.White,40,220);
        }
        protected override void Dispose(bool disposing){if(disposing){timer.Dispose();if(timerPeriod){timeEndPeriod(1);timerPeriod=false;}}base.Dispose(disposing);}
    }
}
