package com.cranklab;

import android.app.Instrumentation;
import android.os.Bundle;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.LinkedHashSet;
import org.json.JSONArray;
import org.json.JSONObject;

/** Reads the installed app's model; no downloads, activity launch or saved translations. */
public final class SpeedProbe extends Instrumentation {
    private Bundle arguments;
    static native byte[] runNative(String model,String backend,String bridge,byte[] input,int repeats);
    @Override public void onCreate(Bundle extras) {arguments=extras;start();}
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            System.load(getContext().getApplicationInfo().nativeLibraryDir+"/libspeedprobe.so");
            String profile=arguments.getString("profile","stq1");
            String file=profile.equals("q4")?"Hy-MT2-1.8B-Q4_K_M.gguf":"Hy-MT2-1.8B-1.25Bit.gguf";
            File storage=new File(getTargetContext().getFilesDir(),"CrankLab/Translation");
            String backend=getTargetContext().getApplicationInfo().nativeLibraryDir;
            String bridge=arguments.getString("bridge",backend+"/libcrank-translate.so");
            String source="A puzzle game for Playdate. Turn the crank to rotate the board and find the path. Use the D-pad to move, press A to select and B to go back. Each level introduces a new challenge. Your progress is saved automatically, so you can stop at any time and continue later. Try to complete all levels with as few moves as possible.";
            String id=arguments.getString("sourceId","");
            if(arguments.getString("case","").equals("controls"))source="Controls: Press A to jump and hold B to brake. Turn the crank clockwise to speed up; turn it counterclockwise to slow down. Do not undock the crank during calibration. Double-tap the screen to exit fullscreen. There are 12 levels, 3 difficulty settings, and no in-app purchases. Your progress is saved automatically. An internet connection is only needed to download new games.";
            if(!id.isEmpty()) {
                if(!id.matches("[0-9]+"))throw new IllegalArgumentException("sourceId must be numeric");
                byte[] json=Files.readAllBytes(new File(storage,"Descriptions/"+id+".json").toPath());
                source=new JSONObject(new String(json,StandardCharsets.UTF_8)).getString("description");
            }
            int repeats=Integer.parseInt(arguments.getString("repeats","2"));
            if(repeats<1 || repeats>3)throw new IllegalArgumentException("repeats must be 1..3");
            String metrics=new String(runNative(new File(storage,"Models/"+file).getAbsolutePath(),backend,bridge,source.getBytes(StandardCharsets.UTF_8),repeats),StandardCharsets.UTF_8);
            JSONArray runs=new JSONArray(metrics);
            boolean passed=runs.length()==repeats;
            for(int i=0;i<runs.length();i++) {
                JSONObject run=runs.getJSONObject(i);
                passed&=run.getInt("code")==0 && run.getInt("outputBytes")>0 && run.getBoolean("utf8Valid");
                if(run.getBoolean("streamAvailable"))passed&=run.getInt("streamUpdates")>0 && run.getBoolean("streamMatchesOutput");
            }
            result.putString("metrics",metrics);result.putString("profile",profile);result.putString("sourceId",id);
            LinkedHashSet<String> cpuLibraries=new LinkedHashSet<>();
            try {
                for(String line:Files.readAllLines(new File("/proc/self/maps").toPath(),StandardCharsets.UTF_8))
                    if(line.contains("/libggml-cpu-"))cpuLibraries.add(line.substring(line.lastIndexOf('/')+1));
            } catch(java.io.IOException ignored) { /* Optional backend diagnostic. */ }
            result.putString("cpuBackendLibraries",new JSONArray(cpuLibraries).toString());
            result.putString("passed",Boolean.toString(passed));finish(passed?-1:0,result);
        } catch(Throwable error) {result.putString("error",error.toString());finish(0,result);}
    }
}
