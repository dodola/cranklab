package com.cranklab;

import android.app.Activity;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.SystemClock;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/** Only model transfers use this Wi-Fi-bound connection; other app traffic is unaffected. */
public final class CrankModelDownload {
    private static final AtomicLong sequence=new AtomicLong();
    private static final ConcurrentHashMap<Long,Job> jobs=new ConcurrentHashMap<>();
    private CrankModelDownload() {}

    static boolean eligible(NetworkCapabilities capabilities) {
        return capabilities!=null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
            && !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
            && !capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)
            && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            && capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED);
    }
    @SuppressWarnings("deprecation")
    private static Network wifi(ConnectivityManager manager) {
        if(manager==null)return null;
        Network active=manager.getActiveNetwork();
        if(eligible(manager.getNetworkCapabilities(active)))return active;
        for(Network network:manager.getAllNetworks()) if(eligible(manager.getNetworkCapabilities(network)))return network;
        return null;
    }
    private static ConnectivityManager manager(Activity activity) {
        return (ConnectivityManager)activity.getApplicationContext().getSystemService(Context.CONNECTIVITY_SERVICE);
    }
    public static boolean hasWifi(Activity activity) {
        try {return activity!=null && wifi(manager(activity))!=null;}
        catch(RuntimeException error) {return false;}
    }
    public static long begin(Activity activity,String address,String path,long size) {
        if(activity==null || size<=0)return -2;
        try {
            ConnectivityManager manager=manager(activity);Network network=wifi(manager);
            if(network==null)return -1;
            boolean debug=(activity.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE)!=0;
            Job job=new Job(manager,network,address,new File(path),size,debug);
            long id=sequence.incrementAndGet();jobs.put(id,job);
            new Thread(job,"Playdate-model-download").start();return id;
        } catch(RuntimeException error) {Log.w("CrankModelDownload","Unable to start model download",error);return -2;}
    }
    public static String[] snapshot(long id) {
        Job job=jobs.get(id);
        return job==null ? new String[]{"4","0","missing"} : new String[]{Integer.toString(job.state),Long.toString(job.received),job.error};
    }
    public static void cancel(long id) {Job job=jobs.remove(id);if(job!=null){job.stop(false);synchronized(job){if(job.state==2)job.path.delete();}}}
    public static void release(long id) {Job job=jobs.remove(id);if(job!=null && job.state==1)job.stop(false);}

    private static final class Job implements Runnable {
        final ConnectivityManager manager;final Network network;final String address;final File path;final long size;final boolean debug;
        volatile int state=1;volatile long received;volatile String error="";
        volatile boolean cancelled,wifiLost;volatile HttpURLConnection connection;
        final long deadline=SystemClock.elapsedRealtime()+1800000;
        Job(ConnectivityManager m,Network n,String u,File p,long s,boolean d) {manager=m;network=n;address=u;path=p;size=s;debug=d;}
        void stop(boolean lost) {
            HttpURLConnection current;
            synchronized(this) {if(lost && state!=1)return;wifiLost|=lost;if(cancelled)return;cancelled=true;current=connection;}
            // Disconnect can wait on a socket read. Never block UE's game/UI thread.
            if(current!=null)new Thread(current::disconnect,"Playdate-model-cancel").start();
        }
        void check() throws IOException {
            if(!eligible(manager.getNetworkCapabilities(network))) {wifiLost=true;cancelled=true;}
            if(cancelled)throw new IOException("cancelled");
            if(SystemClock.elapsedRealtime()>deadline)throw new IOException("timeout");
        }
        HttpURLConnection open() throws IOException {
            URL url=new URL(address);
            for(int redirects=0;redirects<=5;redirects++) {
                check();
                boolean loopback=debug && "http".equals(url.getProtocol()) && ("10.0.2.2".equals(url.getHost()) || "127.0.0.1".equals(url.getHost()));
                if(!"https".equals(url.getProtocol()) && !loopback)throw new IOException("address");
                // DNS, sockets and every redirect stay on the selected Wi-Fi Network.
                HttpURLConnection current=(HttpURLConnection)network.openConnection(url);connection=current;
                current.setInstanceFollowRedirects(false);current.setConnectTimeout(30000);current.setReadTimeout(30000);
                current.setRequestProperty("Accept-Encoding","identity");current.setRequestProperty("User-Agent","Playdate/1.0");
                check();int code=current.getResponseCode();
                if(code==301 || code==302 || code==303 || code==307 || code==308) {
                    String location=current.getHeaderField("Location");current.disconnect();connection=null;
                    if(location==null)throw new IOException("redirect");url=new URL(url,location);continue;
                }
                if(code!=200)throw new IOException("http");
                long length=current.getContentLengthLong();if(length>=0 && length!=size)throw new IOException("size");
                return current;
            }
            throw new IOException("redirect");
        }
        @Override public void run() {
            ConnectivityManager.NetworkCallback callback=new ConnectivityManager.NetworkCallback() {
                @Override public void onLost(Network changed) {if(network.equals(changed))stop(true);}
                @Override public void onCapabilitiesChanged(Network changed,NetworkCapabilities capabilities) {if(network.equals(changed) && !eligible(capabilities))stop(true);}
            };
            boolean registered=false,complete=false;
            try {
                NetworkRequest request=new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                    .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).build();
                manager.registerNetworkCallback(request,callback);registered=true;
                HttpURLConnection current=open();
                try(InputStream input=current.getInputStream();FileOutputStream output=new FileOutputStream(path)) {
                    byte[] buffer=new byte[65536];int count;
                    while(true) {
                        check();count=input.read(buffer);if(count<0)break;check();
                        if(count>size-received)throw new IOException("size");
                        output.write(buffer,0,count);received+=count;
                    }
                    if(received!=size)throw new IOException("size");
                    output.flush();output.getFD().sync();check();
                }
                complete=!cancelled;
            } catch(IOException | RuntimeException failure) {error=failure.getMessage()==null ? "io" : failure.getMessage();}
            finally {
                HttpURLConnection current=connection;connection=null;if(current!=null)current.disconnect();
                if(registered)try {manager.unregisterNetworkCallback(callback);} catch(RuntimeException ignored) {}
                // Publish only after closing the writer; cancellation owns its own partial.
                synchronized(this) {
                    if(!complete || cancelled) {path.delete();state=wifiLost ? 3 : cancelled ? 5 : 4;}
                    else state=2;
                }
            }
        }
    }
}
