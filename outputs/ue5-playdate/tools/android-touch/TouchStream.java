package com.cranklab.tools;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Method;
import java.util.LinkedHashMap;

/** Development-only ADB shell helper. Events are restricted to the app UID.
 * Uses the platform input service without changing permission or API policies.
 * Source: AOSP InputManagerGlobal.injectInputEvent(InputEvent, int, int).
 */
public final class TouchStream {
    private final Object manager;
    private final Method inject;
    private final int uid;
    private long downTime;
    private final LinkedHashMap<Integer, float[]> points = new LinkedHashMap<>();
    private TouchStream(int targetUid) throws Exception {
        uid=targetUid;
        Class<?> type=Class.forName("android.hardware.input.InputManagerGlobal");
        manager=type.getMethod("getInstance").invoke(null);
        inject=type.getMethod("injectInputEvent",InputEvent.class,int.class,int.class);
    }
    private void emit(int action) throws Exception {
        int count=points.size(), i=0;
        MotionEvent.PointerProperties[] properties=new MotionEvent.PointerProperties[count];
        MotionEvent.PointerCoords[] coords=new MotionEvent.PointerCoords[count];
        for(java.util.Map.Entry<Integer,float[]> entry:points.entrySet()) {
            properties[i]=new MotionEvent.PointerProperties();
            properties[i].id=entry.getKey(); properties[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
            coords[i]=new MotionEvent.PointerCoords();
            coords[i].x=entry.getValue()[0]; coords[i].y=entry.getValue()[1];
            coords[i].pressure=1; coords[i].size=1; i++;
        }
        MotionEvent event=MotionEvent.obtain(downTime,SystemClock.uptimeMillis(),action,count,
            properties,coords,0,0,1,1,0,0,InputDevice.SOURCE_TOUCHSCREEN,0);
        try {
            // WAIT_FOR_FINISHED = 2. The target UID prevents delivery elsewhere.
            if(!(Boolean)inject.invoke(manager,event,2,uid)) throw new IllegalStateException("Input target rejected event");
        } finally { event.recycle(); }
    }
    private void command(String line) throws Exception {
        String[] args=line.trim().split("\\s+");
        String action=args[0];
        if(action.equals("cancel")) {
            if(!points.isEmpty()) emit(MotionEvent.ACTION_CANCEL);
            points.clear(); return;
        }
        int id=Integer.parseInt(args[1]);
        if(id<0 || id>9) throw new IllegalArgumentException("Pointer id outside test range");
        if(action.equals("down")) {
            if(points.containsKey(id)) throw new IllegalArgumentException("Pointer already down");
            if(points.isEmpty()) downTime=SystemClock.uptimeMillis();
            points.put(id,new float[]{Float.parseFloat(args[2]),Float.parseFloat(args[3])});
            emit(points.size()==1 ? MotionEvent.ACTION_DOWN :
                 MotionEvent.ACTION_POINTER_DOWN|((points.size()-1)<<MotionEvent.ACTION_POINTER_INDEX_SHIFT));
        } else if(action.equals("move")) {
            if(!points.containsKey(id)) throw new IllegalArgumentException("Pointer not down");
            points.put(id,new float[]{Float.parseFloat(args[2]),Float.parseFloat(args[3])});
            emit(MotionEvent.ACTION_MOVE);
        } else if(action.equals("up")) {
            int index=0; boolean found=false;
            for(int candidate:points.keySet()) { if(candidate==id) { found=true; break; } index++; }
            if(!found) throw new IllegalArgumentException("Pointer not down");
            emit(points.size()==1 ? MotionEvent.ACTION_UP :
                 MotionEvent.ACTION_POINTER_UP|(index<<MotionEvent.ACTION_POINTER_INDEX_SHIFT));
            points.remove(id);
        } else throw new IllegalArgumentException("Unknown touch command");
    }
    public static void main(String[] args) throws Exception {
        if(args.length!=1) throw new IllegalArgumentException("Expected owned application UID");
        TouchStream stream=new TouchStream(Integer.parseInt(args[0]));
        BufferedReader input=new BufferedReader(new InputStreamReader(System.in));
        System.out.println("ready"); System.out.flush();
        try {
            String line;
            while((line=input.readLine())!=null && !line.equals("quit")) {
                stream.command(line); System.out.println("ok"); System.out.flush();
            }
        } finally {
            if(!stream.points.isEmpty()) {
                try { stream.emit(MotionEvent.ACTION_CANCEL); } finally { stream.points.clear(); }
            }
        }
    }
}
