using System;
using System.IO;
using System.Text;
namespace WiredScreen {
    public sealed class EncodedVideoFrame {
        public byte[] Data;
        public long SourceSequence,CapturedQpc,EncodedQpc,Frequency,PacketReadyQpc;
    }
    public sealed class NativePacketReader {
        private readonly Stream input;
        private readonly long frequency;
        private long lastSequence,lastCapture;
        public NativePacketReader(Stream input){
            this.input=input;
            byte[] header=Wire.Exact(input,16);
            if(Encoding.ASCII.GetString(header,0,8)!="WSNGPU01")throw new InvalidDataException("Invalid native stream header");
            frequency=BitConverter.ToInt64(header,8);
            if(frequency<=0)throw new InvalidDataException("Invalid native clock frequency");
        }
        public bool Read(out EncodedVideoFrame frame){
            frame=null;int first=input.ReadByte();if(first<0)return false;
            byte[] header=new byte[32];header[0]=(byte)first;Buffer.BlockCopy(Wire.Exact(input,31),0,header,1,31);
            int length=BitConverter.ToInt32(header,0);long sequence=BitConverter.ToInt64(header,8),captured=BitConverter.ToInt64(header,16),encoded=BitConverter.ToInt64(header,24);
            if(length<=0||length>Wire.Maximum||BitConverter.ToInt32(header,4)!=0||sequence<=lastSequence||captured<=0||captured<lastCapture||encoded<captured)throw new InvalidDataException("Invalid native frame metadata");
            frame=new EncodedVideoFrame{Data=Wire.Exact(input,length),SourceSequence=sequence,CapturedQpc=captured,EncodedQpc=encoded,Frequency=frequency};
            lastSequence=sequence;lastCapture=captured;return true;
        }
    }
}
