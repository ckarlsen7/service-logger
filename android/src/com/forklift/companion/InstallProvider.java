package com.forklift.companion;
import android.content.*;import android.database.*;import android.net.Uri;import android.os.*;import android.provider.OpenableColumns;import java.io.*;
/** Shares only files explicitly placed in app-private cache/exports. */
public class InstallProvider extends ContentProvider {
    public boolean onCreate(){return true;}
    File file(Uri uri)throws FileNotFoundException{
        try{String name=uri.getLastPathSegment();if(name==null||!name.matches("[A-Za-z0-9_.-]+"))throw new IOException();File root=new File(getContext().getCacheDir(),"exports").getCanonicalFile(),f=new File(root,name).getCanonicalFile();if(!f.getParentFile().equals(root)||!f.isFile())throw new IOException();return f;}catch(IOException e){throw new FileNotFoundException();}
    }
    public ParcelFileDescriptor openFile(Uri u,String mode)throws FileNotFoundException{if(!"r".equals(mode))throw new FileNotFoundException();return ParcelFileDescriptor.open(file(u),ParcelFileDescriptor.MODE_READ_ONLY);}
    public String getType(Uri u){return "audio/wav";}
    public Cursor query(Uri u,String[] p,String s,String[] args,String sort){try{File f=file(u);MatrixCursor c=new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE});c.addRow(new Object[]{f.getName(),f.length()});return c;}catch(Exception e){return null;}}
    public Uri insert(Uri u,ContentValues v){throw new UnsupportedOperationException();}public int update(Uri u,ContentValues v,String s,String[] a){throw new UnsupportedOperationException();}public int delete(Uri u,String s,String[] a){throw new UnsupportedOperationException();}
}
