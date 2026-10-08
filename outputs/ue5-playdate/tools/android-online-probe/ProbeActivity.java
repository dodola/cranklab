package com.cranklab.onlineprobe;
import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
public final class ProbeActivity extends Activity {
    public void onCreate(Bundle state) {
        super.onCreate(state);TextView text=new TextView(this);
        text.setText("Playdate online translation test\n\nTesting public game descriptions. Game saves remain unchanged.");
        text.setTextSize(22);text.setPadding(32,64,32,32);setContentView(text);
    }
}
