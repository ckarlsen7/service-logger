package com.forklift.companion;
import java.io.*;

/** Streaming mono IMA ADPCM WAV decoder; no microphone or network access. */
public final class PcmWav {
    private static final int[] STEP={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    private static final int[] INDEX={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
    static int u16(RandomAccessFile f)throws IOException{return f.readUnsignedByte()|(f.readUnsignedByte()<<8);}
    static long u32(RandomAccessFile f)throws IOException{return (long)u16(f)|((long)u16(f)<<16);}
    static String four(RandomAccessFile f)throws IOException{byte[] b=new byte[4];f.readFully(b);return new String(b,"US-ASCII");}
    static void le16(RandomAccessFile f,int x)throws IOException{f.write(x&255);f.write((x>>>8)&255);}
    static void le32(RandomAccessFile f,long x)throws IOException{le16(f,(int)x);le16(f,(int)(x>>>16));}
    static void header(RandomAccessFile f,int rate,long samples)throws IOException{
        f.seek(0);f.writeBytes("RIFF");le32(f,36+samples*2);f.writeBytes("WAVEfmt ");le32(f,16);le16(f,1);le16(f,1);le32(f,rate);le32(f,rate*2L);le16(f,2);le16(f,16);f.writeBytes("data");le32(f,samples*2);
    }
    public static File prepare(File input,File output)throws IOException{
        try(RandomAccessFile f=new RandomAccessFile(input,"r")){
            if(!four(f).equals("RIFF"))throw new IOException("Not a WAV file");u32(f);if(!four(f).equals("WAVE"))throw new IOException("Not a WAV file");
            int tag=0,channels=0,rate=0,align=0,bits=0;long data=-1,size=0,fact=-1;
            while(f.getFilePointer()+8<=f.length()){
                String id=four(f);long n=u32(f),start=f.getFilePointer(),end=start+n;
                if(end<start||end>f.length())throw new IOException("Incomplete WAV chunk");
                if(id.equals("fmt ")&&n>=16){tag=u16(f);channels=u16(f);rate=(int)u32(f);u32(f);align=u16(f);bits=u16(f);}
                else if(id.equals("fact")&&n>=4)fact=u32(f);
                else if(id.equals("data")){data=start;size=n;}
                f.seek(end+(n&1));
            }
            if(data<0||rate<8000||rate>48000||channels!=1)throw new IOException("Expected mono WAV at 8–48 kHz");
            if(tag==1&&bits==16)return input;
            if(tag!=17||bits!=4||align<4||align>4096||size%align!=0)throw new IOException("Unsupported or incomplete WAV encoding");
            long capacity=(size/align)*(1+(align-4)*2L),limit=fact>=0?Math.min(fact,capacity):capacity;
            f.seek(data);long count=0;
            try(RandomAccessFile out=new RandomAccessFile(output,"rw")){
                out.setLength(0);header(out,rate,0);
                for(long block=0;block<size/align&&count<limit;block++){
                    int predictor=(short)u16(f),index=f.readUnsignedByte();f.readUnsignedByte();
                    if(index>88)throw new IOException("Invalid ADPCM step index");
                    le16(out,predictor);count++;
                    for(int i=4;i<align;i++){
                        int packed=f.readUnsignedByte();
                        for(int shift=0;shift<=4;shift+=4){
                            int nib=(packed>>shift)&15,step=STEP[index];
                            int delta=((2*(nib&7)+1)*step)>>3;
                            predictor+=((nib&8)!=0)?-delta:delta;predictor=Math.max(-32768,Math.min(32767,predictor));index=Math.max(0,Math.min(88,index+INDEX[nib]));
                            if(count<limit){le16(out,predictor);count++;}
                        }
                    }
                }
                header(out,rate,count);
            }
            return output;
        }
    }
}
