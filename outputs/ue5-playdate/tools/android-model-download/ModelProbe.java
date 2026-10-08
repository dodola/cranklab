package com.cranklab;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Intent;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.io.FileInputStream;
import java.security.MessageDigest;

/** Emulator only: real Wi-Fi-bound HTTP, redirects, partials and network loss. */
public final class ModelProbe extends Instrumentation {
    int checks;Activity activity;File folder;
    void check(boolean yes,String message) {if(!yes)throw new AssertionError(message);checks++;}
    void shell(String command) throws Exception {try(ParcelFileDescriptor descriptor=getUiAutomation().executeShellCommand(command);FileInputStream input=new FileInputStream(descriptor.getFileDescriptor())){while(input.read()!=-1){}}}
    void wifi(boolean enabled) throws Exception {
        shell("svc wifi "+(enabled?"enable":"disable"));
        long end=System.currentTimeMillis()+15000;
        while(CrankModelDownload.hasWifi(activity)!=enabled && System.currentTimeMillis()<end)Thread.sleep(100);
        check(CrankModelDownload.hasWifi(activity)==enabled,"Wi-Fi state changed");
    }
    long start(String name,String endpoint,long size) {return CrankModelDownload.begin(activity,"http://10.0.2.2:18783/"+endpoint,new File(folder,name).getAbsolutePath(),size);}
    String[] waitFor(long id,boolean firstBytes) throws Exception {
        long end=System.currentTimeMillis()+35000;String[] snapshot;
        do {snapshot=CrankModelDownload.snapshot(id);if(!"1".equals(snapshot[0]) || (firstBytes && Long.parseLong(snapshot[1])>0))return snapshot;Thread.sleep(30);}while(System.currentTimeMillis()<end);
        throw new AssertionError("download timeout");
    }
    @Override public void onCreate(Bundle arguments) {start();}
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            // This probe changes only its disposable emulator's Wi-Fi state.
            check(android.os.Build.MODEL.contains("sdk") || android.os.Build.FINGERPRINT.contains("emulator"),"isolated emulator required");
            activity=startActivitySync(new Intent(getTargetContext(),ModelActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            folder=new File(getTargetContext().getFilesDir(),"model-probe");folder.mkdirs();
            check(!CrankModelDownload.eligible(null),"offline excluded");
            wifi(true);
            ConnectivityManager manager=(ConnectivityManager)getTargetContext().getSystemService(Context.CONNECTIVITY_SERVICE);
            check(CrankModelDownload.eligible(manager.getNetworkCapabilities(manager.getActiveNetwork())),"real validated Wi-Fi allowed");
            long id=start("complete.part","redirect",262144);check(id>0,"Wi-Fi transfer started");String[] completed=waitFor(id,false);
            check("2".equals(completed[0]) && "262144".equals(completed[1]),"redirect and exact persisted byte count");
            File complete=new File(folder,"complete.part");check(complete.length()==262144,"closed file published");
            MessageDigest digest=MessageDigest.getInstance("SHA-256");try(FileInputStream input=new FileInputStream(complete)){byte[] buffer=new byte[8192];int n;while((n=input.read(buffer))>=0)digest.update(buffer,0,n);}
            StringBuilder sha=new StringBuilder();for(byte b:digest.digest())sha.append(String.format("%02x",b));result.putString("fixtureSha256",sha.toString());
            CrankModelDownload.release(id);complete.delete();
            for(String endpoint:new String[]{"short","oversize","error"}) {
                id=start(endpoint+".part",endpoint,262144);String[] failed=waitFor(id,false);
                check("4".equals(failed[0]),endpoint+" rejected");check(!new File(folder,endpoint+".part").exists(),endpoint+" partial removed");CrankModelDownload.release(id);
            }
            id=start("cancel.part","slow",8*1024*1024);String[] received=waitFor(id,true);
            check("1".equals(received[0]) && Long.parseLong(received[1])>0,"progress measured before cancellation");
            CrankModelDownload.cancel(id);Thread.sleep(500);check(!new File(folder,"cancel.part").exists(),"cancel writer closed and partial removed");
            id=start("wifi-loss.part","slow",8*1024*1024);received=waitFor(id,true);check(Long.parseLong(received[1])>0,"Wi-Fi loss fixture receiving");
            wifi(false);String[] lost=waitFor(id,false);
            check("3".equals(lost[0]),"Wi-Fi loss reported");check(!new File(folder,"wifi-loss.part").exists(),"Wi-Fi loss partial removed");CrankModelDownload.release(id);
            shell("svc data enable");long mobileDeadline=System.currentTimeMillis()+10000;NetworkCapabilities mobile;
            do {mobile=manager.getNetworkCapabilities(manager.getActiveNetwork());if(mobile!=null && mobile.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR))break;Thread.sleep(100);}while(System.currentTimeMillis()<mobileDeadline);
            result.putBoolean("cellularDefaultAvailable",mobile!=null && mobile.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR));
            check(!CrankModelDownload.eligible(mobile),"real cellular or offline excluded");
            check(start("mobile.part","forbidden-if-mobile",262144)==-1,"no Wi-Fi blocks before any request");
            check(!new File(folder,"mobile.part").exists(),"cellular created no partial");wifi(true);
            id=start("retry.part","model",262144);check("2".equals(waitFor(id,false)[0]),"retry on Wi-Fi succeeds");CrankModelDownload.release(id);new File(folder,"retry.part").delete();
            result.putInt("checks",checks);result.putBoolean("passed",true);finish(Activity.RESULT_OK,result);
        } catch(Throwable error) {result.putString("error",error.toString());finish(Activity.RESULT_CANCELED,result);}
        finally {try {shell("svc wifi enable");}catch(Exception ignored){} if(activity!=null)runOnMainSync(activity::finish);}
    }
}
