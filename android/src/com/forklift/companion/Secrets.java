package com.forklift.companion;
import android.content.Context;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import java.security.KeyStore;
import javax.crypto.*;
import javax.crypto.spec.GCMParameterSpec;
final class Secrets {
    static javax.crypto.SecretKey key()throws Exception{
        KeyStore ks=KeyStore.getInstance("AndroidKeyStore");ks.load(null);
        if(!ks.containsAlias("forklift-token")){
            KeyGenerator g=KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,"AndroidKeyStore");
            g.init(new KeyGenParameterSpec.Builder("forklift-token",KeyProperties.PURPOSE_ENCRYPT|KeyProperties.PURPOSE_DECRYPT).setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());g.generateKey();
        }
        return (javax.crypto.SecretKey)ks.getKey("forklift-token",null);
    }
    static void save(Context ctx,String value)throws Exception{
        Cipher c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.ENCRYPT_MODE,key());
        String encoded=Base64.encodeToString(c.getIV(),Base64.NO_WRAP)+":"+Base64.encodeToString(c.doFinal(value.getBytes("UTF-8")),Base64.NO_WRAP);
        if(!ctx.getSharedPreferences("connection",0).edit().putString("token",encoded).commit())throw new Exception("Could not save token");
    }
    static String read(Context ctx)throws Exception{
        String stored=ctx.getSharedPreferences("connection",0).getString("token","");if(stored.isEmpty())return "";
        String[] parts=stored.split(":",2);if(parts.length!=2)throw new Exception("Re-enter your device token in Settings");
        Cipher c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.DECRYPT_MODE,key(),new GCMParameterSpec(128,Base64.decode(parts[0],Base64.NO_WRAP)));
        return new String(c.doFinal(Base64.decode(parts[1],Base64.NO_WRAP)),"UTF-8");
    }
}
