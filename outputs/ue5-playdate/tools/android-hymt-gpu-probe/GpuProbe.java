package com.cranklab.hymtgpu;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.LinkedHashSet;
import org.json.JSONArray;
import org.json.JSONObject;

public final class GpuProbe extends Instrumentation {
    private Bundle arguments;
    static native byte[] runNative(String model,String backend,String bridge,byte[] input,int repeats,int layers,String cache);
    @Override public void onCreate(Bundle args) {arguments=args;start();}
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            // A foreground activity pauses UE rendering during both CPU/GPU tests.
            Intent intent=new Intent(getTargetContext(),ProbeActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);startActivitySync(intent);
            File storage=getTargetContext().getFilesDir();
            String backend=getTargetContext().getApplicationInfo().nativeLibraryDir;
            System.load(backend+"/libspeedprobe.so");
            String source="A puzzle game for Playdate. Turn the crank to rotate the board and find the path. Use the D-pad to move, press A to select and B to go back. Each level introduces a new challenge. Your progress is saved automatically, so you can stop at any time and continue later. Try to complete all levels with as few moves as possible.";
            String name=arguments.getString("case","short");
            if(name.equals("controls"))source="Controls: Press A to jump and hold B to brake. Turn the crank clockwise to speed up; turn it counterclockwise to slow down. Do not undock the crank during calibration. Double-tap the screen to exit fullscreen. There are 12 levels, 3 difficulty settings, and no in-app purchases. Your progress is saved automatically. An internet connection is only needed to download new games.";
            if(name.equals("long"))source=new String(Files.readAllBytes(new File(storage,"long.txt").toPath()),StandardCharsets.UTF_8);
            int repeats=Integer.parseInt(arguments.getString("repeats","2"));
            int layers=Integer.parseInt(arguments.getString("layers","100"));
            if(repeats<1||repeats>3||layers<0||layers>100)throw new IllegalArgumentException("invalid benchmark arguments");
            String metrics=new String(runNative(new File(storage,"Hy-MT2-1.8B-Q4_K_M.gguf").getAbsolutePath(),backend,backend+"/libcrank-translate.so",source.getBytes(StandardCharsets.UTF_8),repeats,layers,new File(getTargetContext().getCacheDir(),"opencl").getAbsolutePath()),StandardCharsets.UTF_8);
            JSONArray runs=new JSONArray(metrics);boolean passed=runs.length()==repeats;
            for(int i=0;i<runs.length();i++) {
                JSONObject run=runs.getJSONObject(i);
                passed&=run.getInt("code")==0&&run.getInt("outputBytes")>0&&run.getBoolean("utf8Valid")&&run.getBoolean("streamMatchesOutput");
            }
            result.putString("metrics",metrics);result.putString("case",name);result.putInt("requestedGpuLayers",layers);
            LinkedHashSet<String> libraries=new LinkedHashSet<>();
            for(String line:Files.readAllLines(new File("/proc/self/maps").toPath(),StandardCharsets.UTF_8))
                if(line.contains("OpenCL")||line.contains("vulkan")||line.contains("ggml"))libraries.add(line.substring(line.lastIndexOf('/')+1));
            result.putString("backendLibraries",new JSONArray(libraries).toString());
            result.putString("passed",Boolean.toString(passed));finish(passed?-1:0,result);
        } catch(Throwable error) {result.putString("error",error.toString());finish(0,result);}
    }
}
