package com.wiredscreen.usb;
import java.util.Arrays;
import java.io.IOException;
public final class AvcInitializationTest {
    private static void reject(byte[] data)throws Exception{
        try{AvcInitialization.split(data);}catch(IOException expected){return;}
        throw new AssertionError("Invalid startup accepted");
    }
    public static void main(String[] args)throws Exception{
        byte[] packet={0,0,1,9,16,0,0,0,1,103,66,0,0,1,104,1,0,0,1,101,5,0,0,3,1};
        AvcInitialization result=AvcInitialization.split(packet);
        if(!Arrays.equals(result.config,new byte[]{0,0,0,1,103,66,0,0,0,1,104,1}))throw new AssertionError("CSD ordering/start codes");
        if(!Arrays.equals(result.picture,new byte[]{0,0,1,9,16,0,0,1,101,5,0,0,3,1}))throw new AssertionError("Picture changed");
        reject(new byte[]{0,0,1,101,5});
        byte[] nonIdr=packet.clone();nonIdr[19]=65;reject(nonIdr);
        reject(new byte[]{0,0,1});reject(new byte[]{1,2,3});reject(new byte[0]);
        System.out.println("PASS: CSD split, IDR bytes preserved, escaped bytes, missing config/non-IDR/truncation rejected");
    }
}
