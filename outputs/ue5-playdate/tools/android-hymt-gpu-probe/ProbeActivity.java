package com.cranklab.hymtgpu;
import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
public final class ProbeActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        TextView text=new TextView(this);
        text.setText("HY-MT2 Q4 GPU experiment\n\nIsolated performance test. Playdate game data is preserved.");
        text.setTextSize(22);text.setPadding(32,64,32,32);setContentView(text);
    }
}
