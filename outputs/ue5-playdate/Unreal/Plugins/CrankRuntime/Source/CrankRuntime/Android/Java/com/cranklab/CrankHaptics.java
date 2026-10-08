package com.cranklab;

import android.app.Activity;
import android.content.Context;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;
import java.util.concurrent.atomic.AtomicLong;

/** Hardware-aware feedback for buttons and crank detents (minimum API 26). */
public final class CrankHaptics {
    private static final String TAG="CrankHaptics";
    private static final AtomicLong generation=new AtomicLong();
    private static Vibrator vibrator;
    private static boolean reportedFailure;

    private CrankHaptics() {}

    // Called only on the Android UI thread. Do not retain the Activity.
    private static Vibrator getVibrator(Activity activity) {
        if(vibrator==null) {
            Context context=activity.getApplicationContext();
            if(Build.VERSION.SDK_INT>=31) {
                VibratorManager manager=(VibratorManager)context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
                if(manager!=null) vibrator=manager.getDefaultVibrator();
            }
            if(vibrator==null) vibrator=(Vibrator)context.getSystemService(Context.VIBRATOR_SERVICE);
            if(vibrator!=null) Log.i(TAG,"Hardware: present="+vibrator.hasVibrator()+", amplitude="+vibrator.hasAmplitudeControl());
        }
        return vibrator;
    }

    // Capability inputs keep unsupported primitives out of the generated effect.
    static VibrationEffect createEffect(boolean tick,float strength,boolean primitiveSupported,boolean amplitudeControl) {
        if(Float.isNaN(strength) || Float.isInfinite(strength)) strength=.6f;
        strength=Math.max(.1f,Math.min(1f,strength));
        if(Build.VERSION.SDK_INT>=31 && primitiveSupported) {
            int primitive=tick ? VibrationEffect.Composition.PRIMITIVE_TICK : VibrationEffect.Composition.PRIMITIVE_CLICK;
            return VibrationEffect.startComposition().addPrimitive(primitive,.25f+.75f*strength).compose();
        }
        // Motors without amplitude control need the default amplitude, with
        // strength expressed as duration. Tiny 7 ms pulses may never spin up.
        int amplitude=amplitudeControl ? Math.round((tick ? 72 : 96)+(tick ? 128 : 159)*strength) : VibrationEffect.DEFAULT_AMPLITUDE;
        long duration=Math.round((tick ? 12 : 25)+(tick ? 16 : 20)*strength);
        return VibrationEffect.createOneShot(duration,amplitude);
    }

    @SuppressWarnings("deprecation")
    private static void vibrate(Vibrator hardware,VibrationEffect effect) {
        // Classify as touch feedback: UE's untyped vibrate(effect) uses UNKNOWN.
        // Honor the phone's touch-feedback preferences on both old and new OSes.
        if(Build.VERSION.SDK_INT>=33) hardware.vibrate(effect,new VibrationAttributes.Builder().setUsage(VibrationAttributes.USAGE_TOUCH).build());
        else hardware.vibrate(effect,new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build());
    }

    private static void reportFailure(RuntimeException error) {
        if(!reportedFailure) {reportedFailure=true; Log.w(TAG,"Touch feedback unavailable",error);}
    }

    public static void pulse(Activity activity,boolean tick,float strength) {
        if(activity==null) return;
        final long request=generation.get();
        activity.runOnUiThread(()->{
            // Cancel also invalidates pulses still waiting for the UI thread.
            if(request!=generation.get() || activity.isFinishing() || activity.isDestroyed() || !activity.hasWindowFocus()) return;
            try {
                Vibrator hardware=getVibrator(activity);
                if(hardware==null || !hardware.hasVibrator()) return;
                int primitive=tick ? VibrationEffect.Composition.PRIMITIVE_TICK : VibrationEffect.Composition.PRIMITIVE_CLICK;
                boolean supported=Build.VERSION.SDK_INT>=31 && hardware.arePrimitivesSupported(primitive)[0];
                vibrate(hardware,createEffect(tick,strength,supported,hardware.hasAmplitudeControl()));
            } catch(RuntimeException error) {reportFailure(error);}
        });
    }

    public static void cancel(Activity activity) {
        generation.incrementAndGet();
        if(activity==null) return;
        activity.runOnUiThread(()->{
            try {Vibrator hardware=getVibrator(activity); if(hardware!=null) hardware.cancel();}
            catch(RuntimeException error) {reportFailure(error);}
        });
    }
}
