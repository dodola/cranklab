package com.cranklab;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.os.Bundle;
import android.os.VibrationEffect;
import android.util.Log;

/** Runs the shipped Java bridge, real Android effect factories and its UI queue. */
public final class HapticsProbe extends Instrumentation {
    private int checks;
    private void check(boolean condition,String label) {
        if(!condition) throw new AssertionError(label);
        checks++; Log.i("HapticsProbe","PASS "+label);
    }
    @Override public void onCreate(Bundle extras) {start();}
    @Override public void onStart() {
        Bundle result=new Bundle(); HapticsActivity activity=null;
        try {
            // Unsupported primitives must always yield a one-shot. Motors with
            // no amplitude control must use DEFAULT_AMPLITUDE, not tiny values.
            check(CrankHaptics.createEffect(false,.6f,false,false).equals(VibrationEffect.createOneShot(37,VibrationEffect.DEFAULT_AMPLITUDE)),"button non-amplitude motor");
            check(CrankHaptics.createEffect(true,.6f,false,false).equals(VibrationEffect.createOneShot(22,VibrationEffect.DEFAULT_AMPLITUDE)),"crank non-amplitude motor");
            check(CrankHaptics.createEffect(false,.6f,false,true).equals(VibrationEffect.createOneShot(37,191)),"button amplitude motor");
            check(CrankHaptics.createEffect(true,.6f,false,true).equals(VibrationEffect.createOneShot(22,149)),"crank amplitude motor");
            for(boolean tick:new boolean[]{false,true}) for(boolean amplitude:new boolean[]{false,true}) {
                check(!CrankHaptics.createEffect(tick,.1f,false,amplitude).equals(CrankHaptics.createEffect(tick,1f,false,amplitude)),"strength works: tick="+tick+", amplitude="+amplitude);
                check(CrankHaptics.createEffect(tick,-10f,false,amplitude).equals(CrankHaptics.createEffect(tick,.1f,false,amplitude)),"low strength bounded");
                check(CrankHaptics.createEffect(tick,10f,false,amplitude).equals(CrankHaptics.createEffect(tick,1f,false,amplitude)),"high strength bounded");
            }
            check(CrankHaptics.createEffect(false,Float.NaN,false,true).equals(CrankHaptics.createEffect(false,.6f,false,true)),"invalid saved strength recovered");
            check(CrankHaptics.createEffect(true,1f,true,true).equals(VibrationEffect.startComposition().addPrimitive(VibrationEffect.Composition.PRIMITIVE_TICK,1f).compose()),"supported crank primitive");
            check(CrankHaptics.createEffect(false,1f,true,true).equals(VibrationEffect.startComposition().addPrimitive(VibrationEffect.Composition.PRIMITIVE_CLICK,1f).compose()),"supported button primitive");
            check(!CrankHaptics.createEffect(false,.1f,true,true).equals(CrankHaptics.createEffect(false,1f,true,true)),"native primitive strength works");

            Intent intent=new Intent(getTargetContext(),HapticsActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity=(HapticsActivity)startActivitySync(intent);
            final HapticsActivity target=activity;
            for(int i=0;i<100 && !target.hasWindowFocus();i++) Thread.sleep(50);
            check(target.hasWindowFocus(),"foreground window ready");
            Log.i("HapticsProbe","LIVE button");
            runOnMainSync(()->CrankHaptics.pulse(target,false,.6f)); Thread.sleep(200);
            Log.i("HapticsProbe","LIVE crank");
            runOnMainSync(()->CrankHaptics.pulse(target,true,.6f)); Thread.sleep(200);
            Log.i("HapticsProbe","CANCEL queued pulse");
            runOnMainSync(()->{
                Thread worker=new Thread(()->CrankHaptics.pulse(target,false,1f)); worker.start();
                try {worker.join();} catch(InterruptedException error) {throw new RuntimeException(error);}
                CrankHaptics.cancel(target);
            });
            waitForIdleSync(); Thread.sleep(100);
            Log.i("HapticsProbe","SUPPRESS inactive window");
            runOnMainSync(()->{target.allowFeedback=false; CrankHaptics.pulse(target,false,1f);});
            Log.i("HapticsProbe","SUPPRESS finishing activity");
            runOnMainSync(()->{target.allowFeedback=true; target.finish(); CrankHaptics.pulse(target,false,1f);});
            waitForIdleSync();
            result.putInt("factoryChecks",checks);
            result.putInt("expectedVibrationRequests",2);
            result.putString("systemEvidence","dumpsys vibrator_manager must show exactly two TOUCH requests from this UID");
            finish(Activity.RESULT_OK,result);
        } catch(Throwable error) {
            if(activity!=null) {final Activity target=activity; runOnMainSync(target::finish);}
            result.putString("error",error.toString()); finish(Activity.RESULT_CANCELED,result);
        }
    }
}
