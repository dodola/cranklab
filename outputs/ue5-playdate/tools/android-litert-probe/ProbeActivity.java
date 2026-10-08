package com.cranklab.litertprobe;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowManager;
import android.widget.TextView;

/** Keeps the probe foreground so the UE renderer pauses during isolated tests. */
public final class ProbeActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        TextView status = new TextView(this);
        status.setText("Playdate translation experiment\n\nCPU / GPU benchmark in progress.\nResults are saved locally.");
        status.setPadding(32, 64, 32, 32);
        status.setTextSize(20);
        setContentView(status);
        getExternalFilesDir(null); // Create the owned directory before adb pushes the model.
    }
}
