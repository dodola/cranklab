package com.cranklab;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

/** Only used by the isolated emulator probe. */
public final class HapticsActivity extends Activity {
    volatile boolean allowFeedback=true;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView view=new TextView(this); view.setText("Playdate haptic verification"); setContentView(view);
    }
    @Override public boolean hasWindowFocus() {return allowFeedback && super.hasWindowFocus();}
}
