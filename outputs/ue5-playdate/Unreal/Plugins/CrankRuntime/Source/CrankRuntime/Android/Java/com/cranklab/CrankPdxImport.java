package com.cranklab;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.ByteBuffer;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.net.URL;
import java.net.HttpURLConnection;

/** Copies SAF documents to a physical, private PDX directory for the C VM. */
public final class CrankPdxImport {
    public static final int ZIP_REQUEST=18420, FOLDER_REQUEST=18421;
    private static volatile String[] result={"0","",""};
    private static volatile Uri lastUri;
    private static volatile boolean lastFolder;
    private static volatile String lastUrl;
    private static final long MAX_BYTES=512L*1024*1024;
    private static final int MAX_FILES=20000, MAX_DEPTH=32;
    private CrankPdxImport() {}

    public static String root(Activity activity) {
        return new File(activity.getFilesDir(),"CrankLab").getAbsolutePath();
    }
    public static synchronized boolean open(final Activity activity, final boolean folder) {
        if ("1".equals(result[0]) || "2".equals(result[0])) return false;
        lastUri=null; lastUrl=null;
        result=new String[]{"1","","正在打开文件选择器…"};
        activity.runOnUiThread(()-> {
            try {
                Intent intent=new Intent(folder ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                if (!folder) { intent.addCategory(Intent.CATEGORY_OPENABLE); intent.setType("*/*"); }
                activity.startActivityForResult(intent,folder ? FOLDER_REQUEST : ZIP_REQUEST);
            } catch (Exception error) { failure(error); }
        });
        return true;
    }
    public static boolean onActivityResult(final Activity activity,int request,int code,Intent data) {
        if (request!=ZIP_REQUEST && request!=FOLDER_REQUEST) return false;
        if (code!=Activity.RESULT_OK || data==null || data.getData()==null) {
            result=new String[]{"5","","已取消导入。"}; return true;
        }
        final Uri uri=data.getData(); final boolean folder=request==FOLDER_REQUEST;
        lastUri=uri; lastFolder=folder; lastUrl=null;
        startImport(activity,uri,folder,null); return true;
    }
    public static synchronized boolean download(final Activity activity,final String address) {
        if("1".equals(result[0]) || "2".equals(result[0])) return false;
        lastUri=null; lastUrl=null;
        try {
            URL url=new URL(address);
            if(!url.getProtocol().equals("https") && !url.getProtocol().equals("http")) throw new IOException("请输入 HTTP 或 HTTPS 的 ZIP 游戏直链。");
            lastUri=null; lastUrl=address; lastFolder=false;
            startImport(activity,null,false,address); return true;
        } catch(Exception error) {failure(error); return false;}
    }
    public static synchronized boolean retry(final Activity activity) {
        if(!"4".equals(result[0])) return false;
        if(lastUrl!=null) return download(activity,lastUrl);
        if(lastUri==null) return false;
        startImport(activity,lastUri,lastFolder,null); return true;
    }
    private static void startImport(final Activity activity,final Uri uri,final boolean folder,final String address) {
        result=new String[]{"2","","正在导入游戏…"};
        Thread worker=new Thread(()-> {
            File stage=null;
            try {
                File library=new File(root(activity),"Games");
                if (!library.isDirectory() && !library.mkdirs()) throw new IOException("无法创建游戏目录。");
                File[] previous=library.listFiles();
                if (previous!=null) for (File file:previous) if (file.getName().startsWith(".import-")) eraseStage(file,library);
                stage=new File(library,".import-"+UUID.randomUUID());
                if (!stage.mkdir()) throw new IOException("无法创建临时目录。");
                Budget budget=new Budget(stage);
                if(address!=null) {
                    File archive=new File(stage,".download.zip"); fetchArchive(address,archive);
                    result=new String[]{"2","","下载完成，正在安装游戏…"};
                    try(InputStream input=new FileInputStream(archive)) {extractZip(input,stage,budget);}
                    if(!archive.delete()) throw new IOException("无法清理下载临时文件。");
                } else if (folder) {
                    String id=DocumentsContract.getTreeDocumentId(uri);
                    copyTree(activity.getContentResolver(),uri,id,stage,budget,0,new HashSet<String>());
                } else {
                    try (InputStream input=activity.getContentResolver().openInputStream(uri)) {
                        if (input==null) throw new IOException("无法读取所选文件。");
                        extractZip(input,stage,budget);
                    }
                }
                File installed=install(stage,library);
                eraseStage(stage,library); stage=null;
                result=new String[]{"3",installed.getAbsolutePath(),"游戏已导入，可以在列表中打开。"};
            } catch (Exception error) { failure(error); }
            finally { if (stage!=null) try { eraseStage(stage,stage.getParentFile()); } catch (Exception ignored) {} }
        },"CrankPdxImport");
        worker.setDaemon(true); worker.start();
    }
    private static void fetchArchive(String address,File archive) throws IOException {
        HttpURLConnection connection=(HttpURLConnection)new URL(address).openConnection();
        connection.setConnectTimeout(15000); connection.setReadTimeout(20000); connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("User-Agent","CrankLab/1.0");
        try {
            int status=connection.getResponseCode();
            if(status<200 || status>=300) throw new IOException("下载失败（HTTP "+status+"），请检查游戏直链后重试。");
            long total=connection.getContentLengthLong();
            if(total>MAX_BYTES) throw new IOException("游戏包超过 512 MB 下载上限。");
            try(InputStream input=connection.getInputStream(); OutputStream output=new FileOutputStream(archive)) {
                byte[] buffer=new byte[32768]; int count; long copied=0;
                while((count=input.read(buffer))!=-1) {
                    copied+=count; if(copied>MAX_BYTES) throw new IOException("游戏包超过 512 MB 下载上限。");
                    output.write(buffer,0,count);
                    result=new String[]{"2","","正在下载… "+(copied/1024)+" KB"+(total>0 ? " / "+(total/1024)+" KB" : "")};
                }
                if(total>=0 && copied!=total) throw new IOException("下载不完整，请重试。");
            }
        } finally {connection.disconnect();}
    }
    public static String[] snapshot() {
        String[] current=result;
        return new String[]{current[0],current[1],current[2],"4".equals(current[0]) && (lastUri!=null || lastUrl!=null) ? "1" : "0"};
    }
    public static synchronized void acknowledge() {
        if (Integer.parseInt(result[0])>=3) result=new String[]{"0","",""};
    }
    private static void failure(Exception error) {
        String message=error.getMessage();
        if (message==null || message.isEmpty()) message="导入失败，请重新选择 ZIP 或 PDX 文件夹。";
        result=new String[]{"4","",message};
    }
    private static final class Budget {
        final File root; long bytes; int files,entries;
        final Set<String> paths=new HashSet<>();
        Budget(File root) { this.root=root; }
        void addEntry() throws IOException { if (++entries>MAX_FILES) throw new IOException("游戏文件数量过多。"); }
        void addFile(File destination) throws IOException {
            if (++files>MAX_FILES) throw new IOException("游戏文件数量过多。");
            if (!paths.add(destination.getCanonicalPath())) throw new IOException("游戏包中有重复文件路径。");
        }
        void addBytes(int count) throws IOException {
            bytes+=count;
            if (bytes>MAX_BYTES) throw new IOException("游戏包超过 512 MB 导入上限。");
            result=new String[]{"2","","正在导入游戏… 已读取 "+(bytes/1024)+" KB"};
        }
    }
    private static File child(File root,String path) throws IOException {
        if (path.isEmpty() || path.startsWith("/") || path.indexOf('\\')>=0 || path.indexOf('\0')>=0)
            throw new IOException("游戏包包含无效路径。");
        if (path.matches("^[a-zA-Z]:.*")) throw new IOException("游戏包包含无效路径。");
        String[] parts=path.split("/",-1);
        if (parts.length>MAX_DEPTH) throw new IOException("游戏目录层级过深。");
        for (String part:parts) if (part.equals("..") || part.equals(".") || part.isEmpty())
            throw new IOException("游戏包包含无效路径。");
        File file=new File(root,path);
        if (!file.getCanonicalPath().startsWith(root.getCanonicalPath()+File.separator))
            throw new IOException("游戏包包含越界路径。");
        return file;
    }
    private static void copy(InputStream in,File destination,Budget budget) throws IOException {
        budget.addFile(destination);
        File parent=destination.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("存储空间不足或目录无法写入。");
        try (OutputStream out=new FileOutputStream(destination)) {
            byte[] buffer=new byte[32768]; int count;
            while ((count=in.read(buffer))!=-1) { budget.addBytes(count); out.write(buffer,0,count); }
        }
    }
    private static void extractZip(InputStream input,File root,Budget budget) throws IOException {
        try (ZipInputStream zip=new ZipInputStream(input)) {
            ZipEntry entry; boolean any=false;
            while ((entry=zip.getNextEntry())!=null) {
                budget.addEntry();
                String name=entry.getName();
                if (entry.isDirectory() && name.endsWith("/")) name=name.substring(0,name.length()-1);
                File destination=child(root,name);
                if (name.startsWith("__MACOSX/") || name.equals("__MACOSX") || name.endsWith("/.DS_Store") || name.equals(".DS_Store")) continue;
                if (entry.isDirectory()) {
                    if (!destination.isDirectory() && !destination.mkdirs()) throw new IOException("无法创建游戏目录。");
                } else { copy(zip,destination,budget); any=true; }
                zip.closeEntry();
            }
            if (!any) throw new IOException("所选文件不是可读取的游戏 ZIP。");
        }
    }
    private static void copyTree(ContentResolver resolver,Uri tree,String id,File target,Budget budget,int depth,Set<String> visited) throws IOException {
        if (depth>MAX_DEPTH || !visited.add(id)) throw new IOException("游戏文件夹层级无效。");
        Uri children=DocumentsContract.buildChildDocumentsUriUsingTree(tree,id);
        String[] fields={DocumentsContract.Document.COLUMN_DOCUMENT_ID,DocumentsContract.Document.COLUMN_DISPLAY_NAME,DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor=resolver.query(children,fields,null,null,null)) {
            if (cursor==null) throw new IOException("无法读取所选文件夹。");
            while (cursor.moveToNext()) {
                budget.addEntry();
                String next=cursor.getString(0),name=cursor.getString(1),mime=cursor.getString(2);
                if (name==null || name.indexOf('/')>=0 || name.indexOf('\\')>=0) throw new IOException("游戏文件名无效。");
                File destination=child(budget.root,relative(budget.root,target)+name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!destination.isDirectory() && !destination.mkdirs()) throw new IOException("无法创建游戏目录。");
                    copyTree(resolver,tree,next,destination,budget,depth+1,visited);
                } else {
                    try (InputStream in=resolver.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree,next))) {
                        if (in==null) throw new IOException("无法读取游戏文件。");
                        copy(in,destination,budget);
                    }
                }
            }
        }
    }
    private static String relative(File root,File dir) throws IOException {
        if (root.getCanonicalFile().equals(dir.getCanonicalFile())) return "";
        return dir.getCanonicalPath().substring(root.getCanonicalPath().length()+1)+"/";
    }
    private static boolean luaEntry(File directory) {
        return new File(directory,"main.pdz").isFile() || new File(directory,"main.lua").isFile() || new File(directory,"main.luac").isFile();
    }
    private static boolean nativeEntry(File directory) throws IOException {
        File binary=new File(directory,"pdex.bin");if(!binary.isFile())return false;
        if(binary.length()<48 || binary.length()>64*1024*1024)throw new IOException("pdex.bin 大小无效。");
        byte[] header=new byte[48];try(DataInputStream input=new DataInputStream(new FileInputStream(binary))){input.readFully(header);}
        byte[] magic="Playdate PDX\0".getBytes(StandardCharsets.US_ASCII);
        for(int i=0;i<magic.length;i++)if(header[i]!=magic[i])throw new IOException("不支持的 pdex.bin 格式。");
        ByteBuffer fields=ByteBuffer.wrap(header).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        long code=Integer.toUnsignedLong(fields.getInt(32)),program=Integer.toUnsignedLong(fields.getInt(36));
        long entry=Integer.toUnsignedLong(fields.getInt(40)),relocations=Integer.toUnsignedLong(fields.getInt(44));
        if(code<4 || program<code || program>64*1024*1024 || entry>=code || relocations>code/4 || code+relocations*4>64*1024*1024)
            throw new IOException("pdex.bin 头部无效。");
        return true;
    }
    private static void findPdx(File directory,List<File> found,int depth) throws IOException {
        if (depth>MAX_DEPTH) throw new IOException("游戏文件夹层级过深。");
        if (new File(directory,"pdxinfo").isFile()) { found.add(directory); return; }
        File[] children=directory.listFiles();
        if (children!=null) for (File file:children) if (file.isDirectory() && !file.getName().startsWith(".")) findPdx(file,found,depth+1);
    }
    private static void hash(File root,File directory,MessageDigest digest) throws IOException {
        File[] files=directory.listFiles(); if (files==null) throw new IOException("无法读取游戏目录。");
        Arrays.sort(files,Comparator.comparing(File::getName));
        for (File file:files) {
            if (file.isDirectory()) hash(root,file,digest);
            else {
                byte[] name=file.getAbsolutePath().substring(root.getAbsolutePath().length()+1).getBytes(StandardCharsets.UTF_8);
                digest.update(ByteBuffer.allocate(12).putInt(name.length).putLong(file.length()).array()); digest.update(name);
                try (InputStream input=new FileInputStream(file)) {
                    byte[] buffer=new byte[32768]; int count; while ((count=input.read(buffer))!=-1) digest.update(buffer,0,count);
                }
            }
        }
    }
    private static File install(File stage,File library) throws Exception {
        List<File> candidates=new ArrayList<>(); findPdx(stage,candidates,0);
        if (candidates.isEmpty()) throw new IOException("没有找到 pdxinfo，请选择完整的 PDX 游戏。");
        if (candidates.size()!=1) throw new IOException("包内包含多个游戏，请单独选择一个 PDX 文件夹。");
        File game=candidates.get(0);
        boolean nativeCode=nativeEntry(game);
        if (!luaEntry(game) && !nativeCode) throw new IOException("没有找到 Lua 入口或有效 pdex.bin。");
        File info=new File(game,"pdxinfo");
        if (info.length()>65536) throw new IOException("游戏信息文件无效。");
        Properties metadata=new Properties();
        try (Reader reader=new InputStreamReader(new FileInputStream(info),StandardCharsets.UTF_8)) { metadata.load(reader); }
        String id=metadata.getProperty("bundleID",metadata.getProperty("name","game"));
        id=id.replaceAll("[^a-zA-Z0-9._-]","_"); if (id.length()>64) id=id.substring(0,64);
        if (id.isEmpty() || id.startsWith(".")) id="game";
        MessageDigest digest=MessageDigest.getInstance("SHA-256"); hash(game,game,digest);
        StringBuilder hex=new StringBuilder(); for (byte b:digest.digest()) hex.append(String.format(Locale.ROOT,"%02x",b&255));
        File destination=new File(library,id+"-"+hex.substring(0,12)+".pdx");
        if (destination.isDirectory()) return destination;
        if (!game.renameTo(destination)) throw new IOException("无法完成游戏安装，请检查存储空间。");
        return destination;
    }
    private static void eraseStage(File stage,File library) throws IOException {
        if (!stage.getName().startsWith(".import-") || !stage.getCanonicalPath().startsWith(library.getCanonicalPath()+File.separator))
            throw new IOException("临时目录无效。");
        erase(stage,stage.getCanonicalFile());
    }
    private static void erase(File file,File root) throws IOException {
        String path=file.getCanonicalPath(),base=root.getCanonicalPath();
        if (!path.equals(base) && !path.startsWith(base+File.separator)) throw new IOException("临时文件路径无效。");
        File[] files=file.listFiles(); if (files!=null) for (File child:files) erase(child,root);
        if (file.exists() && !file.delete()) throw new IOException("无法清理导入临时文件。");
    }
}
