using System;
using System.IO;
using System.Collections.Generic;

namespace WiredScreen {
    public sealed class Packet {
        public int Kind, Sequence;
        public long Stamp;
        public byte[] Data;
    }
    public static class Wire {
        public const int Maximum = 4*1024*1024;
        public static byte[] Exact(Stream input,int length) {
            if(length<0||length>Maximum)throw new InvalidDataException("Invalid packet size");
            byte[] data=new byte[length];int pos=0;
            while(pos<length){int n=input.Read(data,pos,length-pos);if(n==0)throw new EndOfStreamException("USB connection closed");pos+=n;}
            return data;
        }
        public static Packet Read(Stream input) {
            byte[] h=Exact(input,20);int length=BitConverter.ToInt32(h,4);
            return new Packet{Kind=BitConverter.ToInt32(h,0),Sequence=BitConverter.ToInt32(h,8),Stamp=BitConverter.ToInt64(h,12),Data=Exact(input,length)};
        }
        public static void Write(Stream output,int kind,int sequence,long stamp,byte[] data) {
            if(data.Length>Maximum)throw new InvalidDataException("Frame too large");
            byte[] h=new byte[20];Buffer.BlockCopy(BitConverter.GetBytes(kind),0,h,0,4);Buffer.BlockCopy(BitConverter.GetBytes(data.Length),0,h,4,4);Buffer.BlockCopy(BitConverter.GetBytes(sequence),0,h,8,4);Buffer.BlockCopy(BitConverter.GetBytes(stamp),0,h,12,8);
            output.Write(h,0,h.Length);output.Write(data,0,data.Length);
        }
        public static void Hello(Stream output) {
            byte[] h=new byte[32];Buffer.BlockCopy(System.Text.Encoding.ASCII.GetBytes("WSCREEN1"),0,h,0,8);
            Buffer.BlockCopy(BitConverter.GetBytes(1920),0,h,8,4);Buffer.BlockCopy(BitConverter.GetBytes(1080),0,h,12,4);Buffer.BlockCopy(BitConverter.GetBytes(60),0,h,16,4);Buffer.BlockCopy(BitConverter.GetBytes(1),0,h,20,4);output.Write(h,0,h.Length);
        }
    }
    // FFmpeg inserts AUD NAL units. Split at AUD boundaries, never in the middle of a frame.
    public sealed class AnnexBFramer {
        private MemoryStream frame=new MemoryStream();
        private int zeros=0;
        private bool header=false,hasPicture=false;
        private readonly Action<byte[]> emit;
        public AnnexBFramer(Action<byte[]> emitFrame){emit=emitFrame;}
        public void Feed(byte[] data,int length){for(int i=0;i<length;i++)Byte(data[i]);}
        private void Byte(byte b){
            if(header){
                header=false;int type=b&31;
                if(type==9&&hasPicture)FlushFrame();
                frame.WriteByte(0);frame.WriteByte(0);frame.WriteByte(0);frame.WriteByte(1);frame.WriteByte(b);
                if(type>=1&&type<=5)hasPicture=true;
            } else if(b==0){zeros++;}
            else if(b==1&&zeros>=2){zeros=0;header=true;}
            else{while(zeros>0){frame.WriteByte(0);zeros--;}frame.WriteByte(b);}
            if(frame.Length+zeros>Wire.Maximum)throw new InvalidDataException("Frame boundary missing or oversized frame");
        }
        private void FlushFrame(){emit(frame.ToArray());frame.SetLength(0);hasPicture=false;}
        public void Finish(){if(hasPicture)FlushFrame();}
    }
    public static class ProtocolTests {
        public static void Run(){
            byte[] source={0,0,0,1,9,0xf0,0,0,1,0x67,0x42,0,0,1,0x65,1,2,3,0,0,0,1,9,0xf0,0,0,1,0x41,4,5};
            byte[][] reference=null;
            for(int chunk=1;chunk<=source.Length;chunk++){
                List<byte[]> frames=new List<byte[]>();AnnexBFramer parser=new AnnexBFramer(frames.Add);
                for(int i=0;i<source.Length;i+=chunk){int n=Math.Min(chunk,source.Length-i);byte[] part=new byte[n];Buffer.BlockCopy(source,i,part,0,n);parser.Feed(part,n);}parser.Finish();
                if(frames.Count!=2)throw new Exception("AUD frame count");
                if(reference==null)reference=frames.ToArray();
                for(int i=0;i<2;i++)if(Convert.ToBase64String(reference[i])!=Convert.ToBase64String(frames[i]))throw new Exception("Chunk boundary changed frame");
            }
            MemoryStream stream=new MemoryStream();Wire.Write(stream,1,42,12345,source);stream.Position=0;Packet packet=Wire.Read(stream);
            if(packet.Sequence!=42||packet.Stamp!=12345||packet.Data.Length!=source.Length)throw new Exception("Packet round trip");
            byte[] bad=new byte[20];Buffer.BlockCopy(BitConverter.GetBytes(-1),0,bad,4,4);
            try{Wire.Read(new MemoryStream(bad));throw new Exception("Negative size accepted");}catch(InvalidDataException){}
            Console.WriteLine("PASS: Annex-B chunk boundaries, packet round trip, malformed size");
        }
    }
}
