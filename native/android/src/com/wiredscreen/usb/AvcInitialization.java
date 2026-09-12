package com.wiredscreen.usb;

import java.io.ByteArrayOutputStream;
import java.io.IOException;

/** Split the initial Annex B access unit into configuration and picture data. */
public final class AvcInitialization {
    public final byte[] config, picture;
    private AvcInitialization(byte[] config,byte[] picture){this.config=config;this.picture=picture;}
    private static int prefix(byte[] data,int at){
        if(at+3<=data.length&&data[at]==0&&data[at+1]==0){
            if(data[at+2]==1)return 3;
            if(at+4<=data.length&&data[at+2]==0&&data[at+3]==1)return 4;
        }
        return 0;
    }
    public static AvcInitialization split(byte[] data)throws IOException{
        ByteArrayOutputStream config=new ByteArrayOutputStream(),picture=new ByteArrayOutputStream();
        boolean sps=false,pps=false,idr=false;
        int at=0;
        while(at<data.length){
            int start=prefix(data,at),body=at+start;
            if(start==0||body>=data.length)throw new IOException("首帧不是完整 Annex B 数据");
            int end=body+1;while(end<data.length&&prefix(data,end)==0)end++;
            int type=data[body]&31;
            if((data[body]&128)!=0)throw new IOException("无效 H.264 NAL 标记");
            if(type==7||type==8){
                if(idr)throw new IOException("初始化参数出现在画面之后");
                if(type==7)sps=true;
                if(type==8){if(!sps)throw new IOException("PPS 之前缺少 SPS");pps=true;}
                config.write(new byte[]{0,0,0,1},0,4);config.write(data,body,end-body);
            }else{
                if(type>=1&&type<=5){
                    if(type!=5||!sps||!pps)throw new IOException("会话必须从带 SPS/PPS 的 IDR 帧开始");
                    idr=true;
                }
                picture.write(data,at,end-at);
            }
            at=end;
        }
        if(!sps||!pps||!idr)throw new IOException("首帧缺少 SPS/PPS 或 IDR，请重新开始会话");
        return new AvcInitialization(config.toByteArray(),picture.toByteArray());
    }
}
