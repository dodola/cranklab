package com.cranklab.probe;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.content.res.AssetManager;
import android.os.Bundle;
import java.io.File;
import java.io.InputStream;
import java.io.FileOutputStream;

/** Test instrumentation only. The product application remains the UE5 project. */
public final class Probe extends Instrumentation {
    private Bundle arguments;
    private static native int run(String game, String save, String output, int ticks);
    @Override public void onCreate(Bundle extras) { arguments=extras; start(); }
    private static void copy(AssetManager assets, String source, File destination) throws Exception {
        String[] children=assets.list(source);
        if(children.length>0) {
            destination.mkdirs();
            for(String child:children) copy(assets,source+"/"+child,new File(destination,child));
        } else {
            destination.getParentFile().mkdirs();
            try(InputStream input=assets.open(source); FileOutputStream output=new FileOutputStream(destination)) {
                byte[] buffer=new byte[16384]; int count;
                while((count=input.read(buffer))!=-1) output.write(buffer,0,count);
            }
        }
    }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            Context context=getTargetContext();
            String game=arguments!=null?arguments.getString("game","fixture"):"fixture";
            if(!game.matches("[a-z0-9-]+")) throw new IllegalArgumentException("Invalid test game key");
            File root=new File(context.getFilesDir(),"verification/"+game); root.mkdirs();
            File pdx=new File(root,"game.pdx"); copy(context.getAssets(),"games/"+game,pdx);
            File saves=new File(root,"saves"); saves.mkdirs();
            System.loadLibrary("crank-core"); System.loadLibrary("crank-probe-jni");
            File prefix=new File(root,"frame");
            int code=run(pdx.getAbsolutePath(),saves.getAbsolutePath(),prefix.getAbsolutePath(),game.equals("pdgames")?1200:game.equals("native-life")?1200:300);
            result.putInt("nativeExitCode",code);
            result.putLong("frameBytes",new File(prefix+".bgra").length());
            result.putLong("audioBytes",new File(prefix+".pcm").length());
            result.putString("evidence",root.getAbsolutePath());
            finish(code==0?Activity.RESULT_OK:Activity.RESULT_CANCELED,result);
        } catch(Throwable error) {
            result.putString("error",error.toString()); finish(Activity.RESULT_CANCELED,result);
        }
    }
}
