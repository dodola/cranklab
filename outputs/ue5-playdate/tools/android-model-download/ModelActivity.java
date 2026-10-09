package com.cranklab;
import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
public final class ModelActivity extends Activity {
    @Override public void onCreate(Bundle saved) {super.onCreate(saved);TextView view=new TextView(this);view.setText("Playdate model download verification");setContentView(view);}
}
