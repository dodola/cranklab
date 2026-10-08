package com.cranklab.onlineprobe;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.UUID;
import org.json.JSONArray;
import org.json.JSONObject;

public final class OnlineProbe extends Instrumentation {
    private Bundle arguments;
    public void onCreate(Bundle args){arguments=args;start();}
    public void onStart() {
        Bundle result=new Bundle();JSONArray runs=new JSONArray();
        try {
            Intent intent=new Intent(getTargetContext(),ProbeActivity.class);intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);startActivitySync(intent);
            JSONArray cases=new JSONArray(new String(Files.readAllBytes(new File(getTargetContext().getFilesDir(),"cases.json").toPath()),StandardCharsets.UTF_8));
            String provider=arguments.getString("provider","microsoft");
            int repeats=Integer.parseInt(arguments.getString("repeats","1"));
            if(repeats<1||repeats>3)throw new IllegalArgumentException("repeats must be 1..3");
            boolean passed=true;
            for(int r=0;r<repeats;r++)for(int i=0;i<cases.length();i++) {
                JSONObject item=cases.getJSONObject(i);JSONObject run=new JSONObject();run.put("case",item.getString("name"));run.put("provider",provider);run.put("run",r);
                HttpURLConnection connection=null;long start=System.nanoTime();
                try {
                    Object body;String endpoint;
                    if(provider.equals("microsoft")) {endpoint="https://edge.microsoft.com/translate/translatetext?from=en&to=zh-Hans&isEnterpriseClient=false";body=new JSONArray().put(item.getString("input"));}
                    else {
                        endpoint="https://transmart.qq.com/api/imt";
                        body=new JSONObject().put("header",new JSONObject().put("fn","auto_translation").put("client_key","browser-chrome-110.0.0-Android-"+UUID.randomUUID()+"-"+System.currentTimeMillis())).put("type","plain").put("model_category","normal").put("source",new JSONObject().put("text_list",new JSONArray().put(item.getString("input"))).put("lang","en")).put("target",new JSONObject().put("lang","zh"));
                    }
                    connection=(HttpURLConnection)new URL(endpoint).openConnection();connection.setConnectTimeout(6000);connection.setReadTimeout(10000);connection.setRequestMethod("POST");connection.setDoOutput(true);
                    connection.setRequestProperty("Content-Type","application/json; charset=utf-8");
                    connection.setRequestProperty("User-Agent","Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/110.0.0.0 Mobile Safari/537.36");
                    if(provider.equals("tencent"))connection.setRequestProperty("Referer","https://transmart.qq.com/zh-CN/index");
                    byte[] bytes=body.toString().getBytes(StandardCharsets.UTF_8);connection.setFixedLengthStreamingMode(bytes.length);
                    try(java.io.OutputStream output=connection.getOutputStream()){output.write(bytes);}
                    int code=connection.getResponseCode();run.put("http",code);
                    if(code!=200)throw new java.io.IOException("HTTP "+code);
                    ByteArrayOutputStream buffer=new ByteArrayOutputStream();
                    try(InputStream input=connection.getInputStream()){byte[] block=new byte[8192];int n;while((n=input.read(block))!=-1){if(buffer.size()+n>1024*1024)throw new java.io.IOException("response too large");buffer.write(block,0,n);}}
                    String json=new String(buffer.toByteArray(),StandardCharsets.UTF_8);
                    String text=provider.equals("microsoft")?new JSONArray(json).getJSONObject(0).getJSONArray("translations").getJSONObject(0).getString("text"):new JSONObject(json).getJSONArray("auto_translation").getString(0);
                    run.put("translation",text);passed&=!text.isEmpty();
                }catch(Throwable error){run.put("error",error.toString());passed=false;}
                finally{if(connection!=null)connection.disconnect();run.put("seconds",(System.nanoTime()-start)/1e9);runs.put(run);}
            }
            result.putString("runs",runs.toString());result.putString("passed",Boolean.toString(passed));finish(passed?-1:0,result);
        }catch(Throwable error){result.putString("error",error.toString());finish(0,result);}
    }
}
