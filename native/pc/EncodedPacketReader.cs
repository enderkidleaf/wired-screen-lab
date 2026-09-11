using System;
using System.IO;
using System.Text;

namespace WiredScreen {
    // Streaming AVI wraps each encoded video packet in a length-delimited
    // 00dc/00db chunk. No next-frame marker or end-of-stream is needed.
    public sealed class EncodedPacketReader {
        private readonly Stream input;
        private bool movie;
        public EncodedPacketReader(Stream input) {
            this.input=input;
            byte[] header=Wire.Exact(input,12);
            if(Tag(header,0)!="RIFF"||Tag(header,8)!="AVI ")throw new InvalidDataException("Expected streaming AVI");
        }
        private static string Tag(byte[] data,int offset){return Encoding.ASCII.GetString(data,offset,4);}
        private void Skip(uint count){
            if(count>16*1024*1024)throw new InvalidDataException("Oversized AVI metadata");
            byte[] scratch=new byte[4096];
            while(count>0){int n=input.Read(scratch,0,(int)Math.Min((uint)scratch.Length,count));if(n==0)throw new EndOfStreamException("Truncated AVI metadata");count-=(uint)n;}
        }
        public bool Read(out byte[] packet){
            packet=null;
            while(true){
                int first=input.ReadByte();if(first<0)return false;
                byte[] header=new byte[8];header[0]=(byte)first;
                Buffer.BlockCopy(Wire.Exact(input,7),0,header,1,7);
                string tag=Tag(header,0);uint length=BitConverter.ToUInt32(header,4);
                if(tag=="RIFF"||tag=="LIST"){
                    if(length<4)throw new InvalidDataException("Invalid AVI list");
                    string kind=Tag(Wire.Exact(input,4),0);
                    if(tag=="RIFF"){
                        if(kind!="AVIX")throw new InvalidDataException("Unexpected AVI segment");
                        movie=false;continue;
                    }
                    if(kind=="movi"){movie=true;continue;}
                    if(kind=="rec "&&movie)continue;
                    Skip(length-4);if((length&1)!=0)Skip(1);continue;
                }
                if(movie&&(tag=="00dc"||tag=="00db")){
                    if(length>Wire.Maximum)throw new InvalidDataException("Oversized encoded packet");
                    if(length==0)continue; // AVI may represent a repeated frame as an empty chunk.
                    packet=Wire.Exact(input,(int)length);
                    if((length&1)!=0)Skip(1);
                    return true;
                }
                Skip(length);if((length&1)!=0)Skip(1);
            }
        }
    }
}
