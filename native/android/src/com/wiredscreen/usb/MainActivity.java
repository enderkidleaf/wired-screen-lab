package com.wiredscreen.usb;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.content.res.ColorStateList;
import android.widget.Button;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.net.LocalServerSocket;
import android.net.LocalSocket;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.ConcurrentHashMap;
import org.json.JSONObject;

public class MainActivity extends Activity implements SurfaceHolder.Callback {
    private static final int MAX_PACKET=4*1024*1024;
    private final Handler ui=new Handler(Looper.getMainLooper());
    private SurfaceView video;
    private TextView status,details,controls,awakeButton;
    private LinearLayout panel;
    private ScrollView panelScroll;
    private boolean panelHidden,keepAwake;
    private int dp(int value){return Math.round(value*getResources().getDisplayMetrics().density);}
    private GradientDrawable background(int color,int radius){
        GradientDrawable shape=new GradientDrawable();shape.setColor(color);shape.setCornerRadius(dp(radius));return shape;
    }
    private TextView label(String text,int size,int color){
        TextView view=new TextView(this);view.setText(text);view.setTextSize(size);view.setTextColor(color);return view;
    }
    private TextView action(String text){
        Button view=new Button(this);view.setText(text);view.setTextSize(13);view.setTextColor(Color.rgb(181,239,218));view.setAllCaps(false);view.setMinWidth(0);view.setGravity(Gravity.CENTER);
        view.setMinHeight(dp(48));view.setPadding(dp(14),0,dp(14),0);
        view.setBackground(new RippleDrawable(ColorStateList.valueOf(0x4469d8b0),background(0xff243c43,12),null));view.setContentDescription(text);return view;
    }
    private void setPanelHidden(boolean hidden){
        panelHidden=hidden;panelScroll.setVisibility(hidden?View.GONE:View.VISIBLE);
        controls.setVisibility(hidden?View.VISIBLE:View.GONE);
        getPreferences(MODE_PRIVATE).edit().putBoolean("panelHidden",hidden).apply();
    }
    private void applyAwake(){
        if(keepAwake)getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        awakeButton.setText(keepAwake?"保持亮屏 · 开":"保持亮屏 · 关");
        awakeButton.setContentDescription(awakeButton.getText());
        getPreferences(MODE_PRIVATE).edit().putBoolean("keepAwake",keepAwake).apply();
    }
    private volatile boolean resumed=false;
    private volatile int epoch=0;
    private volatile LocalServerSocket listener;
    private volatile LocalSocket connection;
    private Thread worker;
    private String session="";

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        keepAwake=getPreferences(MODE_PRIVATE).getBoolean("keepAwake",true);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        FrameLayout frame=new FrameLayout(this);frame.setBackgroundColor(Color.BLACK);
        video=new SurfaceView(this);video.getHolder().addCallback(this);
        frame.addView(video,new FrameLayout.LayoutParams(-1,-1,Gravity.CENTER));
        panel=new LinearLayout(this);panel.setOrientation(LinearLayout.VERTICAL);panel.setPadding(dp(20),dp(16),dp(20),dp(16));
        panel.setBackground(background(0xee14232e,20));panel.setElevation(dp(8));
        TextView heading=label("WIRED SCREEN  /  USB 副屏",17,Color.WHITE);heading.setTypeface(null,Typeface.BOLD);panel.addView(heading);
        status=label("等待连接",14,Color.rgb(198,219,228));status.setPadding(0,dp(10),0,dp(12));panel.addView(status);
        details=label("连接后显示帧率和解码信息",12,Color.rgb(143,175,190));details.setVisibility(View.GONE);details.setPadding(0,0,0,dp(12));panel.addView(details);
        LinearLayout actions=new LinearLayout(this);actions.setOrientation(LinearLayout.HORIZONTAL);
        TextView detailButton=action("详细信息"),hideButton=action("隐藏面板");
        detailButton.setOnClickListener(v->{boolean visible=details.getVisibility()!=View.VISIBLE;details.setVisibility(visible?View.VISIBLE:View.GONE);detailButton.setText(visible?"收起详情":"详细信息");detailButton.setContentDescription(detailButton.getText());});
        hideButton.setOnClickListener(v->setPanelHidden(true));
        LinearLayout.LayoutParams half=new LinearLayout.LayoutParams(0,dp(48),1);half.setMargins(0,0,dp(8),0);actions.addView(detailButton,half);
        actions.addView(hideButton,new LinearLayout.LayoutParams(0,dp(48),1));panel.addView(actions);
        awakeButton=action("保持亮屏");LinearLayout.LayoutParams awakeLayout=new LinearLayout.LayoutParams(-1,dp(48));awakeLayout.topMargin=dp(8);panel.addView(awakeButton,awakeLayout);
        awakeButton.setOnClickListener(v->{keepAwake=!keepAwake;applyAwake();});applyAwake();
        TextView hint=label("USB 调试授权后，在电脑点击「启动 USB 副屏」。",12,Color.rgb(143,175,190));hint.setPadding(0,dp(12),0,0);panel.addView(hint);
        FrameLayout.LayoutParams panelLayout=new FrameLayout.LayoutParams(dp(340),-2,Gravity.TOP|Gravity.LEFT);panelLayout.setMargins(dp(20),dp(16),dp(20),dp(16));panelScroll=new ScrollView(this);panelScroll.setFillViewport(false);panelScroll.addView(panel);frame.addView(panelScroll,panelLayout);
        controls=action("控制");controls.setContentDescription("展开副屏控制面板");
        FrameLayout.LayoutParams controlLayout=new FrameLayout.LayoutParams(dp(80),dp(48),Gravity.TOP|Gravity.RIGHT);controlLayout.setMargins(dp(16),dp(16),dp(20),0);frame.addView(controls,controlLayout);
        controls.setOnClickListener(v->setPanelHidden(false));
        frame.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->{
            int width=Math.min(dp(340),Math.max(dp(120),r-l-dp(40)));
            if(panelScroll.getLayoutParams().width!=width){FrameLayout.LayoutParams bounds=(FrameLayout.LayoutParams)panelScroll.getLayoutParams();bounds.width=width;panelScroll.setLayoutParams(bounds);}
        });setContentView(frame);
        setPanelHidden(getPreferences(MODE_PRIVATE).getBoolean("panelHidden",false));
        session=getIntent().getStringExtra("session");
        show("USB 副屏 · 请在 PC 程序点击开始。无需 Wi-Fi 或网络共享。");
    }
    @Override protected void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);stopReceiver();session=intent.getStringExtra("session");startReceiver();}
    @Override protected void onResume(){super.onResume();resumed=true;startReceiver();}
    @Override protected void onPause(){resumed=false;stopReceiver();super.onPause();}
    public void surfaceCreated(SurfaceHolder h){startReceiver();}
    public void surfaceChanged(SurfaceHolder h,int f,int w,int z){}
    public void surfaceDestroyed(SurfaceHolder h){stopReceiver();}
    private void show(String text){ui.post(()->{status.setText(text);details.setText("等待新的连接统计");controls.setText("未连接");controls.setTextColor(Color.rgb(240,199,137));controls.setContentDescription("未连接。展开控制面板查看连接提示");});Log.i("WiredScreen",text);}
    private synchronized void startReceiver(){
        if(!resumed||!video.getHolder().getSurface().isValid()||worker!=null)return;
        if(session==null||!session.matches("[0-9a-f]{32}")){show("USB 副屏 · 请从 PC 程序开始连接。");return;}
        final String token=session;final int run=++epoch;
        worker=new Thread(()->{
            try{
                LocalServerSocket own=new LocalServerSocket("wiredscreen_"+token);listener=own;
                show("USB 通道已就绪 · 等待电脑视频");
                while(run==epoch&&resumed){
                    LocalSocket socket=own.accept();connection=socket;
                    try{receive(socket,run);}catch(Exception ex){if(run==epoch)show("连接结束："+ex.getMessage()+"。请在 PC 重新开始。");}
                    finally{try{socket.close();}catch(Exception ignored){}connection=null;}
                }
            }catch(Exception ex){if(run==epoch)show("USB 接收错误："+ex.getMessage());}
            finally{
                synchronized(MainActivity.this){if(listener!=null){try{listener.close();}catch(Exception ignored){}listener=null;}worker=null;}
                if(run!=epoch&&resumed)ui.post(()->startReceiver());
            }
        },"usb-receiver");worker.start();
    }
    private synchronized void stopReceiver(){
        epoch++;
        try{if(connection!=null)connection.close();}catch(Exception ignored){}
        try{if(listener!=null)listener.close();}catch(Exception ignored){}
    }
    private static byte[] exact(InputStream in,int count)throws IOException{
        byte[] b=new byte[count];int pos=0;
        while(pos<count){int n=in.read(b,pos,count-pos);if(n<0)throw new EOFException("USB 已断开");pos+=n;}return b;
    }
    private static void packet(OutputStream out,int type,int sequence,long stamp,byte[] bytes)throws IOException{
        ByteBuffer h=ByteBuffer.allocate(20).order(ByteOrder.LITTLE_ENDIAN);
        h.putInt(type).putInt(bytes.length).putInt(sequence).putLong(stamp);
        synchronized(out){out.write(h.array());out.write(bytes);out.flush();}
    }
    private void fitVideo(int w,int h){
        ui.post(()->{
            View parent=(View)video.getParent();int pw=parent.getWidth(),ph=parent.getHeight();
            if(pw<=0||ph<=0)return;
            double scale=Math.min((double)pw/w,(double)ph/h);
            video.setLayoutParams(new FrameLayout.LayoutParams((int)(w*scale),(int)(h*scale),Gravity.CENTER));
        });
    }
    private void receive(LocalSocket socket,int run)throws Exception{
        InputStream in=socket.getInputStream();OutputStream out=socket.getOutputStream();
        out.write("WREADY01".getBytes(StandardCharsets.US_ASCII));out.flush();
        ByteBuffer hello=ByteBuffer.wrap(exact(in,32)).order(ByteOrder.LITTLE_ENDIAN);
        byte[] magic=new byte[8];hello.get(magic);
        if(!new String(magic,StandardCharsets.US_ASCII).equals("WSCREEN2"))throw new IOException("协议不匹配，请同时更新电脑和手机 App");
        int width=hello.getInt(),height=hello.getInt(),fps=hello.getInt(),format=hello.getInt();
        if(width!=1920||height!=1080||fps!=60||format!=1)throw new IOException("不支持的画面参数");
        final MediaCodec decoder=MediaCodec.createDecoderByType("video/avc");
        final String decoderName=decoder.getName();
        MediaFormat config=MediaFormat.createVideoFormat("video/avc",width,height);
        config.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE,MAX_PACKET);
        config.setInteger(MediaFormat.KEY_PRIORITY,0);
        config.setInteger(MediaFormat.KEY_FRAME_RATE,fps);
        config.setFloat(MediaFormat.KEY_OPERATING_RATE,(float)fps);
        boolean lowLatency=false;
        if(Build.VERSION.SDK_INT>=30){
            MediaCodecInfo.CodecCapabilities caps=decoder.getCodecInfo().getCapabilitiesForType("video/avc");
            if(caps.isFeatureSupported(MediaCodecInfo.CodecCapabilities.FEATURE_LowLatency)){config.setInteger(MediaFormat.KEY_LOW_LATENCY,1);lowLatency=true;}
        }
        final boolean low=lowLatency;
        AtomicLong decoded=new AtomicLong(),rendered=new AtomicLong();
        AtomicBoolean draining=new AtomicBoolean(true);
        decoder.configure(config,video.getHolder().getSurface(),null,0);
        final ConcurrentHashMap<Long,long[]> frameTimes=new ConcurrentHashMap<>();
        final HandlerThread renderEvents=new HandlerThread("render-events");renderEvents.start();
        decoder.setOnFrameRenderedListener((c,pts,ns)->{
            rendered.incrementAndGet();
            long[] timing=frameTimes.remove(pts);
            if(timing!=null){
                long now=System.nanoTime();
                {
                    byte[] metrics=ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN).putLong(ns-timing[2]).putLong(now-ns).array();
                    try{packet(out,5,(int)timing[0],timing[1],metrics);}catch(IOException e){try{socket.close();}catch(IOException ignored){}}
                }
            }
        },new Handler(renderEvents.getLooper()));
        decoder.start();fitVideo(width,height);
        Thread drain=new Thread(()->{
            MediaCodec.BufferInfo info=new MediaCodec.BufferInfo();
            try{while(draining.get()&&run==epoch){
                int index=decoder.dequeueOutputBuffer(info,10000);
                if(index>=0){decoded.incrementAndGet();decoder.releaseOutputBuffer(index,true);}
                else if(index==MediaCodec.INFO_OUTPUT_FORMAT_CHANGED){Log.i("WiredScreen","Output "+decoder.getOutputFormat());}
            }}catch(Exception e){if(draining.get())Log.e("WiredScreen","Decoder drain",e);draining.set(false);try{socket.close();}catch(Exception ignored){}}
        },"video-output");drain.start();
        long last=System.nanoTime(),lastDecoded=0,lastRendered=0,received=0,lastBytes=0;
        long inputWaitNs=0,inputWaitMaxNs=0,inputSamples=0;
        boolean initialized=false;
        try{
            while(run==epoch&&resumed&&draining.get()){
                ByteBuffer header=ByteBuffer.wrap(exact(in,20)).order(ByteOrder.LITTLE_ENDIAN);
                int type=header.getInt(),size=header.getInt(),sequence=header.getInt();long stamp=header.getLong();
                if(size<0||size>MAX_PACKET)throw new IOException("数据包大小异常");
                byte[] data=exact(in,size);
                if(type==1){
                    long receivedAt=System.nanoTime();
                    if(!initialized){
                        AvcInitialization initial=AvcInitialization.split(data);
                        int configIndex=decoder.dequeueInputBuffer(1000000);
                        if(configIndex<0)throw new IOException("解码器初始化缓冲超时");
                        ByteBuffer configBuffer=decoder.getInputBuffer(configIndex);
                        if(configBuffer==null||configBuffer.capacity()<initial.config.length)throw new IOException("解码初始化缓冲不足");
                        configBuffer.clear();configBuffer.put(initial.config);
                        decoder.queueInputBuffer(configIndex,0,initial.config.length,0,MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                        data=initial.picture;initialized=true;
                        Log.i("WiredScreen","AVC initialization: codec-config submitted before IDR");
                    }
                    long pts=(long)sequence*1000000/60;
                    // Older Android versions may omit callbacks. Bound diagnostic
                    // state independently of the decoder; never drop video here.
                    if(frameTimes.size()>=256)frameTimes.clear();
                    frameTimes.put(pts,new long[]{sequence,stamp,receivedAt});
                    long inputStarted=System.nanoTime();
                    int index=decoder.dequeueInputBuffer(1000000);
                    long inputWait=System.nanoTime()-inputStarted;
                    inputWaitNs+=inputWait;inputWaitMaxNs=Math.max(inputWaitMaxNs,inputWait);inputSamples++;
                    if(index<0)throw new IOException("解码器阻塞，请降低负载后重试");
                    ByteBuffer buffer=decoder.getInputBuffer(index);if(buffer==null||buffer.capacity()<data.length)throw new IOException("解码缓冲不足");
                    buffer.clear();buffer.put(data);decoder.queueInputBuffer(index,0,data.length,(long)sequence*1000000/60,0);received+=size;
                }else if(type==2){packet(out,3,sequence,stamp,new byte[0]);}
                else throw new IOException("未知数据包");
                long now=System.nanoTime();double seconds=(now-last)/1e9;
                if(seconds>=1){
                    long d=decoded.get(),r=rendered.get();
                    double decodeFps=(d-lastDecoded)/seconds,renderFps=(r-lastRendered)/seconds;
                    JSONObject stats=new JSONObject();stats.put("width",width);stats.put("height",height);stats.put("decodedFps",decodeFps);stats.put("renderedFps",renderFps);stats.put("decoded",d);stats.put("rendered",r);stats.put("mbps",(received-lastBytes)*8/seconds/1e6);stats.put("decoder",decoderName);stats.put("lowLatency",low);stats.put("elapsedRealtimeMs",android.os.SystemClock.elapsedRealtime());
                    stats.put("inputWaitMeanMs",inputSamples==0?0:inputWaitNs/1e6/inputSamples);
                    stats.put("inputWaitMaxMs",inputWaitMaxNs/1e6);
                    stats.put("inputSamples",inputSamples);
                    packet(out,4,sequence,stamp,stats.toString().getBytes(StandardCharsets.UTF_8));
                    inputWaitNs=0;inputWaitMaxNs=0;inputSamples=0;
                    String diagnostic=String.format(java.util.Locale.US,"解码 %.1f / 呈现回调 %.1f fps\n%s\n低延迟模式：%s",decodeFps,renderFps,decoderName,low?"开启":"未提供");
                    ui.post(()->{status.setText("已连接 · USB 直连\n1920 × 1080 · 目标 60 帧");details.setText(diagnostic);controls.setText("控制");controls.setTextColor(Color.rgb(181,239,218));controls.setContentDescription("USB 已连接。展开副屏控制面板");});
                    last=now;lastDecoded=d;lastRendered=r;lastBytes=received;
                }
            }
        }finally{draining.set(false);drain.join(1500);try{decoder.stop();}finally{decoder.release();renderEvents.quitSafely();frameTimes.clear();}}
    }
}

