#pragma once
// Include AFTER logger globals and helper functions, BEFORE setup().
// Local Wi-Fi only. Never port-forward port 80 or publish it through a tunnel.
#include <WebServer.h>
#include <ESPmDNS.h>
static WebServer companionHttp(80);
static bool companionStarted = false;
static String companionLastIP;

static String apiQuote(const String &value) {
  String out="\"";
  for(size_t i=0;i<value.length();i++) {
    unsigned char c=value[i];
    if(c=='"'||c=='\\') {out+='\\';out+=(char)c;}
    else if(c=='\n')out+="\\n";
    else if(c=='\r')out+="\\r";
    else if(c=='\t')out+="\\t";
    else if(c<32)out+=' ';
    else out+=(char)c;
  }
  return out+'"';
}
static bool apiId(const String &value) {
  if(value.length()<5||value.length()>90)return false;
  for(size_t i=0;i<value.length();i++) {
    char c=value[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
  }
  return true;
}
static bool apiNote(const String &value) {
  if(!value.startsWith("note")||!value.endsWith(".wav")||value.length()>110)return false;
  for(size_t i=0;i<value.length();i++) {
    char c=value[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
  }
  return value.indexOf("..")<0;
}
static void apiError(int code,const String &message) {
  companionHttp.sendHeader("Cache-Control","no-store");
  companionHttp.send(code,"application/json","{\"error\":"+apiQuote(message)+"}");
}
static bool apiReady() {
  String supplied=companionHttp.header("X-Device-Token");
  if(strlen(DEVICE_TOKEN)==0||supplied!=String(DEVICE_TOKEN)) {apiError(401,"Unauthorized");return false;}
  if(recording||shutdownRequested.load()) {apiError(409,"Logger is recording or shutting down; try again when idle");return false;}
  if(!sdOK) {apiError(503,"SD card unavailable");return false;}
  return true;
}
static String apiVisit(const String &sid,bool detail,bool &truncated) {
  String folder="/jobs/"+sid;
  String status=SD_MMC.exists(folder+"/finished.ok")?"sent":
    (SD_MMC.exists(folder+"/close.req")||SD_MMC.exists(folder+"/visit.req"))?"queued":"not submitted";
  String out="{\"id\":"+apiQuote(sid)+",\"svo\":\"\",\"status\":"+apiQuote(status)+",\"source\":\"logger\",\"recordings\":[";
  File dir=SD_MMC.open(folder);bool first=true;int count=0;
  if(dir) {
    File child=dir.openNextFile();
    while(child) {
      String name=basename(String(child.name()));
      if(!child.isDirectory()&&apiNote(name)) {
        if(count>=256) {truncated=true;child.close();break;}
        count++;
        if(detail) {if(!first)out+=',';first=false;
        out+="{\"name\":"+apiQuote(name)+",\"bytes\":"+String((uint32_t)child.size())+",\"url\":"+apiQuote("/v1/audio/"+sid+"/"+name)+"}";
        }
      }
      child.close();child=dir.openNextFile();
    }
    child.close();dir.close();
  }
  out+="],\"recording_count\":"+String(count);
  if(detail)out+=",\"transcript\":\"\",\"message\":\"Transcripts and SVO assignment are available from the Windows receiver after processing.\"";
  if(detail)out+=",\"battery\":"+batteryJson();
  out+=",\"truncated\":"+String(truncated?"true":"false")+"}";
  return out;
}
static void apiRoute() {
  if(!apiReady())return;
  String uri=companionHttp.uri();
  if(uri=="/v1/health"&&companionHttp.method()==HTTP_GET) {
    companionHttp.send(200,"application/json","{\"name\":\"Forklift logger\",\"api\":1,\"battery\":"+batteryJson()+"}");return;
  }
  if(uri=="/v1/sessions"&&companionHttp.method()==HTTP_GET) {
    String out="{\"items\":[";bool first=true,truncated=false;int count=0;
    File root=SD_MMC.open("/jobs");
    if(root) {
      File job=root.openNextFile();
      while(job) {
        String sid=basename(String(job.name()));bool directory=job.isDirectory();job.close();
        if(directory&&apiId(sid)) {
          if(count>=128) {truncated=true;break;}
          if(!first)out+=',';first=false;count++;
          out+=apiVisit(sid,false,truncated);
        }
        job=root.openNextFile();
      }
      job.close();root.close();
    }
    out+="],\"battery\":"+batteryJson()+",\"truncated\":"+String(truncated?"true":"false")+"}";
    companionHttp.sendHeader("Cache-Control","no-store");companionHttp.send(200,"application/json",out);return;
  }
  String prefix="/v1/sessions/";
  if(uri.startsWith(prefix)&&companionHttp.method()==HTTP_GET) {
    String sid=uri.substring(prefix.length());
    if(!apiId(sid)) {apiError(400,"Invalid visit ID");return;}
    File folder=SD_MMC.open("/jobs/"+sid);bool ok=folder&&folder.isDirectory();folder.close();
    if(!ok) {apiError(404,"Visit not found");return;}
    bool truncated=false;companionHttp.sendHeader("Cache-Control","no-store");companionHttp.send(200,"application/json",apiVisit(sid,true,truncated));return;
  }
  prefix="/v1/audio/";
  if(uri.startsWith(prefix)) {
    String rest=uri.substring(prefix.length());int slash=rest.indexOf('/');
    if(slash<0) {apiError(400,"Invalid recording path");return;}
    String sid=rest.substring(0,slash),name=rest.substring(slash+1);
    if(!apiId(sid)||!apiNote(name)) {apiError(400,"Invalid recording path");return;}
    String path="/jobs/"+sid+"/"+name;
    if(companionHttp.method()==HTTP_GET) {
      File f=SD_MMC.open(path,FILE_READ);
      if(!f||f.isDirectory()) {f.close();apiError(404,"Recording not found");return;}
      companionHttp.sendHeader("Cache-Control","no-store");companionHttp.streamFile(f,"audio/wav");f.close();return;
    }
    if(companionHttp.method()==HTTP_DELETE) {
      if(sid==activeJob) {apiError(409,"Active visit cannot be removed. Queue or finish the visit first.");return;}
      if(!SD_MMC.exists(path)) {apiError(404,"Recording not found");return;}
      SD_MMC.mkdir("/trash");SD_MMC.mkdir("/trash/"+sid);
      String dest="/trash/"+sid+"/"+name;
      if(SD_MMC.exists(dest))dest="/trash/"+sid+"/"+String((uint32_t)esp_random(),HEX)+"_"+name;
      if(!SD_MMC.rename(path,dest)) {apiError(500,"Could not move recording to trash");return;}
      companionHttp.send(200,"application/json","{\"ok\":true,\"message\":\"Moved to SD trash. Receiver copy unchanged.\"}");return;
    }
  }
  apiError(404,"Not found");
}
static void companionPoll() {
  if(!companionStarted) {
    const char *headers[]={"X-Device-Token"};companionHttp.collectHeaders(headers,1);
    companionHttp.onNotFound(apiRoute);companionHttp.begin();companionStarted=true;
  }
  if(WiFi.status()==WL_CONNECTED) {
    String ip=WiFi.localIP().toString();
    if(ip!=companionLastIP) {companionLastIP=ip;MDNS.end();if(MDNS.begin("forklift-logger"))MDNS.addService("forklift","tcp",80);Serial.println("PHONE APP LOGGER ADDRESS: http://"+ip);}
    // Requests run on the main loop, serialized with uploads and SD writes.
    companionHttp.handleClient();
  }
}
