import java.io.*;
import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.util.zip.*;
import com.sun.net.httpserver.HttpServer;

/** Uses the product downloader, extractor and installer against a local server. */
public final class CrankDownloadTest {
    public static void main(String[] args) throws Exception {
        File root=new File(args[0]); root.mkdirs();
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try(ZipOutputStream zip=new ZipOutputStream(bytes)) {
            String[][] files={{"game.pdx/pdxinfo","name=Download fixture\nbundleID=com.cranklab.download"},{"game.pdx/main.lua","playdate.update=function()end"},{"game.pdx/data.bin","\u0000binary"}};
            for(String[] file:files) {zip.putNextEntry(new ZipEntry(file[0])); zip.write(file[1].getBytes("UTF-8")); zip.closeEntry();}
        }
        byte[] payload=bytes.toByteArray();
        HttpServer server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
        server.createContext("/game.zip",exchange->{exchange.sendResponseHeaders(200,payload.length); try(OutputStream out=exchange.getResponseBody()){out.write(payload);} exchange.close();});
        server.createContext("/redirect",exchange->{exchange.getResponseHeaders().add("Location","/game.zip"); exchange.sendResponseHeaders(302,-1); exchange.close();});
        server.createContext("/missing",exchange->{exchange.sendResponseHeaders(404,-1); exchange.close();});
        server.createContext("/oversize",exchange->{exchange.sendResponseHeaders(200,513L*1024*1024); exchange.close();});
        server.createContext("/truncated",exchange->{exchange.sendResponseHeaders(200,payload.length+20); try(OutputStream out=exchange.getResponseBody()){out.write(payload);} catch(IOException expected){} exchange.close();});
        server.start();
        try {
            Class<?> importer=Class.forName("com.cranklab.CrankPdxImport"), budget=Class.forName("com.cranklab.CrankPdxImport$Budget");
            Method fetch=importer.getDeclaredMethod("fetchArchive",String.class,File.class); fetch.setAccessible(true);
            Method extract=importer.getDeclaredMethod("extractZip",InputStream.class,File.class,budget); extract.setAccessible(true);
            Method install=importer.getDeclaredMethod("install",File.class,File.class); install.setAccessible(true);
            Constructor<?> makeBudget=budget.getDeclaredConstructor(File.class); makeBudget.setAccessible(true);
            String base="http://127.0.0.1:"+server.getAddress().getPort();
            for(String path:new String[]{"/game.zip","/redirect"}) {
                File archive=new File(root,path.equals("/game.zip") ? "direct.zip" : "redirect.zip"); fetch.invoke(null,base+path,archive);
                if(!java.util.Arrays.equals(payload,Files.readAllBytes(archive.toPath()))) throw new AssertionError("Downloaded bytes changed");
                File stage=new File(root,".import-"+archive.getName()),library=new File(root,"library"); stage.mkdirs(); library.mkdirs();
                try(InputStream input=new FileInputStream(archive)){extract.invoke(null,input,stage,makeBudget.newInstance(stage));}
                File target=(File)install.invoke(null,stage,library);
                if(Files.readAllBytes(new File(target,"data.bin").toPath())[0]!=0) throw new AssertionError("Installed binary changed");
                System.out.println("PASSED\t"+path+"\tdownload-extract-install");
            }
            for(String path:new String[]{"/missing","/oversize","/truncated"}) {
                try {fetch.invoke(null,base+path,new File(root,path.substring(1)+".zip")); throw new AssertionError("Accepted "+path);}
                catch(InvocationTargetException expected) {if(!(expected.getCause() instanceof IOException)) throw expected;}
                System.out.println("REJECTED\t"+path);
            }
            System.out.println("PASSED\tlocal-download-contract");
        } finally {server.stop(0);}
    }
}
