package com.forklift.companion;

import android.app.*;
import android.content.*;
import android.graphics.Color;
import android.media.MediaPlayer;
import android.net.Uri;
import android.net.nsd.*;
import android.os.*;
import android.text.InputType;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.net.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Consumer;

public class MainActivity extends Activity {
    final int BG=Color.rgb(16,24,32),CARD=Color.rgb(27,39,49),INK=Color.rgb(235,242,246),MUTED=Color.rgb(162,181,191),ACCENT=Color.rgb(77,217,179);
    LinearLayout root,body; TextView heading,notice,playing; ProgressBar progress; SeekBar seek;
    final ExecutorService io=Executors.newSingleThreadExecutor(); final Handler ui=new Handler(Looper.getMainLooper());
    final ArrayDeque<Runnable> history=new ArrayDeque<>(); Runnable current; int generation=0,audioGeneration=0;
    NsdManager nsd; NsdManager.DiscoveryListener discovery;
    String receiver="",logger="",token=""; MediaPlayer player; File pendingSave;
    final Runnable ticker=new Runnable(){public void run(){if(player!=null){try{seek.setMax(player.getDuration());seek.setProgress(player.getCurrentPosition());}catch(Exception ignored){}}ui.postDelayed(this,500);}};

    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        receiver=getPreferences().getString("receiver","");logger=getPreferences().getString("logger","");
        try{token=Secrets.read(this);}catch(Exception e){token="";}
        root=new LinearLayout(this);root.setOrientation(1);root.setBackgroundColor(BG);root.setPadding(dp(16),dp(12),dp(16),dp(8));
        root.setOnApplyWindowInsetsListener((v,insets)->{root.setPadding(dp(16),insets.getSystemWindowInsetTop()+dp(8),dp(16),Math.max(dp(8),insets.getSystemWindowInsetBottom()));return insets;});
        setContentView(root);
        TextView brand=label("FORKLIFT  /  COMPANION",13,ACCENT);root.addView(brand);
        heading=label("Your service notes",25,INK);root.addView(heading);
        LinearLayout tabs=row();tabs.addView(button("SVOs",()->home(false)),weight());tabs.addView(button("Visits",()->homeVisits(false)),weight());tabs.addView(button("Logger SD",()->homeVisits(true)),weight());root.addView(tabs);
        LinearLayout actions=row();actions.addView(button("‹ Back",this::goBack),weight());actions.addView(button("Refresh",()->{if(current!=null)current.run();}),weight());actions.addView(button("Settings",this::settings),weight());root.addView(actions);
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);progress.setIndeterminate(true);progress.setVisibility(View.GONE);root.addView(progress,new LinearLayout.LayoutParams(-1,dp(3)));
        notice=label("",13,MUTED);root.addView(notice);
        ScrollView scroll=new ScrollView(this);body=new LinearLayout(this);body.setOrientation(1);scroll.addView(body);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        playing=label("No recording playing",13,MUTED);root.addView(playing);
        seek=new SeekBar(this);root.addView(seek);seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int value,boolean user){if(user&&player!=null)try{player.seekTo(value);}catch(Exception ignored){}}public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}});
        LinearLayout controls=row();controls.addView(button("Pause / resume",()->{if(player!=null)try{if(player.isPlaying())player.pause();else player.start();}catch(Exception e){showError(e);}}),weight());controls.addView(button("Stop",this::stopAudio),weight());root.addView(controls);
        ui.post(ticker);
        if(receiver.isEmpty()||token.isEmpty()){home(false);settings();}else home(false);
    }
    android.content.SharedPreferences getPreferences(){return getSharedPreferences("connection",0);}
    int dp(int n){return (int)(n*getResources().getDisplayMetrics().density+0.5f);}
    LinearLayout.LayoutParams weight(){return new LinearLayout.LayoutParams(0,-2,1);}
    LinearLayout row(){LinearLayout r=new LinearLayout(this);r.setOrientation(0);return r;}
    TextView label(String value,int size,int color){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setPadding(0,dp(6),0,dp(6));return t;}
    Button button(String title,Runnable action){Button b=new Button(this);b.setText(title);b.setTextSize(12);b.setAllCaps(false);b.setTextColor(INK);b.setOnClickListener(v->action.run());return b;}
    LinearLayout card(String title,String subtitle){LinearLayout c=new LinearLayout(this);c.setOrientation(1);c.setPadding(dp(14),dp(10),dp(14),dp(10));c.setBackgroundColor(CARD);LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.setMargins(0,dp(8),0,dp(4));body.addView(c,p);c.addView(label(title,18,INK));if(!subtitle.isEmpty())c.addView(label(subtitle,13,MUTED));return c;}
    void textBlock(String title,String text){if(text.isEmpty())return;LinearLayout c=card(title,"");TextView value=label(text,15,INK);value.setTextIsSelectable(true);c.addView(value);}
    String segment(String s){return Uri.encode(s);}
    void home(boolean ignored){history.clear();current=()->showSvos();current.run();}
    void homeVisits(boolean local){history.clear();current=()->showVisits(local);current.run();}
    void navigate(Runnable route){if(current!=null)history.push(current);current=route;route.run();}
    void goBack(){if(!history.isEmpty()){current=history.pop();current.run();}else home(false);}
    @Override public void onBackPressed(){if(!history.isEmpty())goBack();else super.onBackPressed();}
    void showError(Exception e){notice.setText(e.getMessage()==null?e.toString():e.getMessage());}
    interface Work {JSONObject run()throws Exception;}
    void load(String title,Work work,Consumer<JSONObject> render){
        int ticket=++generation;heading.setText(title);body.removeAllViews();notice.setText("Loading…");progress.setVisibility(View.VISIBLE);
        io.execute(()->{try{JSONObject obj=work.run();ui.post(()->{if(ticket!=generation||isFinishing())return;progress.setVisibility(View.GONE);notice.setText("");try{render.accept(obj);}catch(Exception e){showError(e);}});}catch(Exception e){ui.post(()->{if(ticket!=generation||isFinishing())return;progress.setVisibility(View.GONE);showError(e);});}});
    }
    void showSvos(){
        final String base=receiver,auth=token;
        load("Service work orders",()->json(base,"/v1/svos",auth,"GET",false),obj->{
            JSONArray items=obj.optJSONArray("items");if(items==null||items.length()==0){card("No SVOs yet","Choose Visits to see pending or unassigned recordings.");return;}
            for(int i=0;i<items.length();i++){JSONObject item=items.optJSONObject(i);if(item==null)continue;String id=item.optString("id");LinearLayout c=card(id,item.optString("model")+" · "+item.optInt("visits")+" visits · "+(item.optBoolean("closed")?"Closed":"Open"));c.addView(button("Open full transcript",()->navigate(()->showSvo(id))));}
        });
    }
    void showSvo(String id){
        final String base=receiver,auth=token;
        load(id,()->json(base,"/v1/svos/"+segment(id),auth,"GET",false),obj->{
            textBlock("Entire SVO transcript",obj.optString("transcript","No transcript available yet."));JSONArray items=obj.optJSONArray("items");
            if(items!=null)for(int i=0;i<items.length();i++){JSONObject v=items.optJSONObject(i);if(v==null)continue;String sid=v.optString("id");LinearLayout c=card(sid,v.optString("status")+" · "+v.optString("sent_at"));c.addView(button("Visit, transcript & recordings",()->navigate(()->showVisit(false,sid))));}
        });
    }
    void batteryCard(JSONObject battery){
        if(battery==null){card("Logger battery","Battery telemetry needs the companion firmware update.");return;}
        int percent=battery.optInt("percent",-1),mv=battery.optInt("millivolts",-1);
        String detail=percent<0?"Reading unavailable":String.format(Locale.US,"About %d%% · %.2f V · voltage estimate",percent,mv/1000.0);
        LinearLayout c=card("Logger battery",detail);
        ProgressBar bar=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);bar.setMax(100);bar.setProgress(Math.max(0,Math.min(100,percent)));bar.setProgressTintList(android.content.res.ColorStateList.valueOf(percent>=0&&percent<=15?Color.rgb(255,155,95):ACCENT));c.addView(bar,new LinearLayout.LayoutParams(-1,dp(12)));
        c.addView(label("Refresh to update. USB/charging and load affect this estimate.",12,MUTED));
    }
    void showVisits(boolean local){
        final String base=local?logger:receiver,auth=token;
        load(local?"Logger SD card":"All receiver visits",()->json(base,"/v1/sessions",auth,"GET",local),obj->{
            if(local)batteryCard(obj.optJSONObject("battery"));
            JSONArray items=obj.optJSONArray("items");if(items==null||items.length()==0){card("No visits found",local?"Record a note on the logger, then refresh.":"Upload a recording from the logger, then refresh.");return;}
            if(local)notice.setText(obj.optBoolean("truncated")?"Large SD library: showing the first 128 visits / 256 notes per visit. Use the SD card for older files.":"Same Wi-Fi/hotspot required. SD recordings may not have transcripts yet.");
            for(int i=0;i<items.length();i++){JSONObject v=items.optJSONObject(i);if(v==null)continue;String sid=v.optString("id"),svo=v.optString("svo");JSONArray rec=v.optJSONArray("recordings");LinearLayout c=card(svo.isEmpty()?"SVO unassigned":svo,sid+"\n"+v.optString("status")+" · "+v.optInt("recording_count",rec==null?0:rec.length())+" recordings");c.addView(button("Open visit",()->navigate(()->showVisit(local,sid))));}
        });
    }
    void showVisit(boolean local,String sid){
        final String base=local?logger:receiver,auth=token;
        load(local?"SD recording visit":"Receiver visit",()->json(base,"/v1/sessions/"+segment(sid),auth,"GET",local),obj->{
            if(local)batteryCard(obj.optJSONObject("battery"));
            if(obj.optBoolean("truncated"))notice.setText("Showing the first 256 recordings in this SD visit.");
            String svo=obj.optString("svo");card(svo.isEmpty()?"SVO unassigned":svo,sid+"\n"+obj.optString("status")+" · "+(local?"Logger SD card":"Windows receiver"));
            textBlock("Processing message",obj.optString("message"));String transcript=obj.optString("transcript");
            if(transcript.isEmpty())card("Transcript unavailable",local?"Full transcripts are on the receiver after upload and transcription.":"The receiver has not created a transcript for this visit yet.");else textBlock("Full visit transcript",transcript);
            textBlock("Technician report",obj.optString("technician_report"));
            JSONArray rec=obj.optJSONArray("recordings");if(rec==null||rec.length()==0)card("No audio files","Transcripts remain available after audio removal.");
            if(rec!=null)for(int i=0;i<rec.length();i++){JSONObject a=rec.optJSONObject(i);if(a==null)continue;String name=a.optString("name"),path="/v1/audio/"+segment(sid)+"/"+segment(name);LinearLayout c=card(name,String.format(Locale.US,"%.1f KB",a.optLong("bytes")/1024.0));LinearLayout buttons=row();buttons.addView(button("Play",()->fetchAudio(base,path,auth,local,name,false)),weight());buttons.addView(button("Save WAV",()->fetchAudio(base,path,auth,local,name,true)),weight());buttons.addView(button("Remove",()->confirmRemove(base,path,auth,local,name)),weight());c.addView(buttons);}
        });
    }
    void confirmRemove(String base,String path,String auth,boolean local,String name){
        String message=local?"Move this SD-card recording to /trash? This will NOT delete the receiver copy or transcript. Recording and uploads must be idle. Unuploaded audio will no longer be queued.":"Move this receiver recording to data/companion_trash? Its transcript and logger SD copy remain. Queued and processing visits cannot be removed.";
        new AlertDialog.Builder(this).setTitle("Remove "+name+"?").setMessage(message).setNegativeButton("Cancel",null).setPositiveButton("Move to trash",(d,w)->{
            notice.setText("Removing…");io.execute(()->{try{JSONObject obj=json(base,path,auth,"DELETE",local);ui.post(()->{Toast.makeText(this,obj.optString("message","Moved to trash"),Toast.LENGTH_LONG).show();if(current!=null)current.run();});}catch(Exception e){ui.post(()->showError(e));}});
        }).show();
    }
    void stopAudio(){audioGeneration++;if(player!=null){try{player.stop();}catch(Exception ignored){}player.release();player=null;}if(playing!=null)playing.setText("No recording playing");if(seek!=null)seek.setProgress(0);}
    void fetchAudio(String base,String path,String auth,boolean local,String name,boolean save){
        if(!save)stopAudio();final int ticket=audioGeneration;notice.setText("Downloading recording…");
        io.execute(()->{try{
            File exports=new File(getCacheDir(),"exports");exports.mkdirs();File raw=new File(exports,UUID.randomUUID()+".wav");
            try(InputStream in=open(base,path,auth,"GET",local);OutputStream out=new FileOutputStream(raw)){copy(in,out,32*1024*1024);}
            if(save){ui.post(()->{pendingSave=raw;Intent intent=new Intent(Intent.ACTION_CREATE_DOCUMENT);intent.addCategory(Intent.CATEGORY_OPENABLE);intent.setType("audio/wav");intent.putExtra(Intent.EXTRA_TITLE,name);notice.setText("Choose where to save the original WAV.");startActivityForResult(intent,20);});}
            else {File pcm=PcmWav.prepare(raw,new File(exports,UUID.randomUUID()+"_pcm.wav"));ui.post(()->{if(ticket!=audioGeneration||isFinishing())return;try{player=new MediaPlayer();player.setDataSource(pcm.getAbsolutePath());player.setOnPreparedListener(mp->{mp.start();playing.setText(name);notice.setText("Playing · "+(local?"Logger SD":"Receiver"));});player.setOnCompletionListener(mp->playing.setText("Finished: "+name));player.setOnErrorListener((mp,what,extra)->{stopAudio();notice.setText("Playback failed ("+what+", "+extra+")");return true;});player.prepareAsync();}catch(Exception e){stopAudio();showError(e);}});}
        }catch(Exception e){ui.post(()->showError(e));}});
    }
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==20&&result==RESULT_OK&&data!=null&&pendingSave!=null){File file=pendingSave;Uri uri=data.getData();io.execute(()->{try(InputStream in=new FileInputStream(file);OutputStream out=getContentResolver().openOutputStream(uri)){if(out==null)throw new IOException("Could not open selected file");copy(in,out,32*1024*1024);ui.post(()->notice.setText("Original WAV saved."));}catch(Exception e){ui.post(()->showError(e));}});}pendingSave=null;}
    static void copy(InputStream in,OutputStream out,int limit)throws IOException{byte[] buf=new byte[16384];int n,total=0;while((n=in.read(buf))!=-1){total+=n;if(total>limit)throw new IOException("File exceeds download limit");out.write(buf,0,n);}}
    static String validate(String value,boolean local)throws Exception{
        value=value.trim();while(value.endsWith("/"))value=value.substring(0,value.length()-1);if(value.isEmpty())throw new Exception(local?"Enter the logger's local IP in Settings":"Enter the receiver's Cloudflare URL in Settings");
        URL u=new URL(value);if(u.getUserInfo()!=null||u.getQuery()!=null||u.getRef()!=null||!u.getPath().isEmpty())throw new Exception("Use a base address with no path, username, or query");
        if(!local&&!u.getProtocol().equals("https"))throw new Exception("Receiver requires HTTPS");
        if(local){if(!u.getProtocol().equals("http")||!privateIp(u.getHost()))throw new Exception("Logger must use http:// with a private IPv4 address, e.g. http://10.0.0.57");}
        return value;
    }
    static boolean privateIp(String host){try{String[] p=host.split("\\.");if(p.length!=4)return false;int[] n=new int[4];for(int i=0;i<4;i++){if(!p[i].matches("[0-9]{1,3}"))return false;n[i]=Integer.parseInt(p[i]);if(n[i]>255)return false;}return n[0]==10||(n[0]==192&&n[1]==168)||(n[0]==172&&n[1]>=16&&n[1]<=31);}catch(Exception e){return false;}}
    static InputStream open(String base,String path,String auth,String method,boolean local)throws Exception{
        if(auth.isEmpty())throw new Exception("Enter your device token in Settings");URL url=new URL(validate(base,local)+path);HttpURLConnection c=(HttpURLConnection)url.openConnection();c.setInstanceFollowRedirects(false);c.setConnectTimeout(15000);c.setReadTimeout(45000);c.setRequestMethod(method);c.setRequestProperty("X-Device-Token",auth);c.setRequestProperty("ngrok-skip-browser-warning","true");
        int code=c.getResponseCode();if(code<200||code>=300){InputStream err=c.getErrorStream();ByteArrayOutputStream bytes=new ByteArrayOutputStream();if(err!=null)try{copy(err,bytes,8192);}finally{err.close();}c.disconnect();String detail=bytes.toString("UTF-8");if(code==401)throw new Exception("Unauthorized: device token does not match the receiver/logger");if(code==404)throw new Exception("API not found. Start the companion receiver, or install the companion logger firmware.");throw new Exception("HTTP "+code+": "+detail.substring(0,Math.min(500,detail.length())));}
        return new FilterInputStream(c.getInputStream()){@Override public void close()throws IOException{try{super.close();}finally{c.disconnect();}}};
    }
    static JSONObject json(String base,String path,String auth,String method,boolean local)throws Exception{try(InputStream in=open(base,path,auth,method,local);ByteArrayOutputStream b=new ByteArrayOutputStream()){copy(in,b,16*1024*1024);return new JSONObject(b.toString("UTF-8"));}}
    EditText field(LinearLayout layout,String title,String value,boolean secret){layout.addView(label(title,14,MUTED));EditText e=new EditText(this);e.setTextColor(INK);e.setText(value);e.setSingleLine(true);e.setInputType(secret?InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_PASSWORD:InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_VARIATION_URI);layout.addView(e);return e;}
    void stopDiscovery(){if(nsd!=null&&discovery!=null){try{nsd.stopServiceDiscovery(discovery);}catch(Exception ignored){}discovery=null;}}
    void discoverLogger(EditText field){
        stopDiscovery();nsd=(NsdManager)getSystemService(NSD_SERVICE);
        discovery=new NsdManager.DiscoveryListener(){
            public void onDiscoveryStarted(String type){runOnUiThread(()->Toast.makeText(MainActivity.this,"Searching local Wi-Fi for logger…",Toast.LENGTH_SHORT).show());}
            public void onServiceFound(NsdServiceInfo info){if(!info.getServiceType().contains("_forklift._tcp"))return;nsd.resolveService(info,new NsdManager.ResolveListener(){
                public void onResolveFailed(NsdServiceInfo service,int code){}
                public void onServiceResolved(NsdServiceInfo service){String host=service.getHost().getHostAddress();if(privateIp(host))runOnUiThread(()->{field.setText("http://"+host+(service.getPort()==80?"":":"+service.getPort()));Toast.makeText(MainActivity.this,"Logger found. Tap Save.",Toast.LENGTH_LONG).show();stopDiscovery();});}
            });}
            public void onServiceLost(NsdServiceInfo info){}
            public void onDiscoveryStopped(String type){}
            public void onStartDiscoveryFailed(String type,int code){runOnUiThread(()->Toast.makeText(MainActivity.this,"Discovery unavailable. Enter the IP printed by the logger.",Toast.LENGTH_LONG).show());stopDiscovery();}
            public void onStopDiscoveryFailed(String type,int code){}
        };
        nsd.discoverServices("_forklift._tcp.",NsdManager.PROTOCOL_DNS_SD,discovery);
        ui.postDelayed(()->stopDiscovery(),12000);
    }
    void settings(){
        LinearLayout form=new LinearLayout(this);form.setOrientation(1);form.setPadding(dp(20),dp(8),dp(20),dp(8));
        EditText r=field(form,"Receiver HTTPS address",receiver,false),l=field(form,"Logger local address (optional)",logger,false),t=field(form,"Device token — enter locally",token,true);
        form.addView(button("Find logger on this Wi-Fi",()->discoverLogger(l)));
        form.addView(button("Clear downloaded audio cache",()->{
            stopAudio();pendingSave=null;io.execute(()->{File dir=new File(getCacheDir(),"exports");File[] files=dir.listFiles();if(files!=null)for(File f:files)if(f.isFile())f.delete();ui.post(()->Toast.makeText(this,"Downloaded audio cache cleared. Saved documents are unchanged.",Toast.LENGTH_LONG).show());});
        }));
        form.addView(label("Receiver works over internet. Logger SD access needs the same Wi-Fi or hotspot. Credentials are encrypted with Android Keystore.",13,MUTED));
        ScrollView scroll=new ScrollView(this);scroll.addView(form);
        AlertDialog dialog=new AlertDialog.Builder(this).setTitle("Connections").setView(scroll).setNegativeButton("Cancel",null).setPositiveButton("Save",null).create();
        dialog.setOnShowListener(d->dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{try{String rb=r.getText().toString().trim(),lb=l.getText().toString().trim(),tk=t.getText().toString().trim();if(!rb.isEmpty())rb=validate(rb,false);if(!lb.isEmpty())lb=validate(lb,true);if(tk.isEmpty())throw new Exception("Enter the same device token used by the logger and receiver");Secrets.save(this,tk);getPreferences().edit().putString("receiver",rb).putString("logger",lb).commit();receiver=rb;logger=lb;token=tk;dialog.dismiss();stopAudio();if(current!=null)current.run();}catch(Exception e){Toast.makeText(this,e.getMessage(),Toast.LENGTH_LONG).show();}}));dialog.show();
    }
    @Override protected void onDestroy(){stopDiscovery();generation++;stopAudio();ui.removeCallbacks(ticker);io.shutdownNow();super.onDestroy();}
}
