import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.*;

/** Runs the product's ZIP/install implementation, not a second implementation. */
public final class CrankImportTest {
    static final Class<?> Importer, Budget;
    static final Constructor<?> BudgetConstructor;
    static final Method Extract, Install;
    static {
        try {
            Importer=Class.forName("com.cranklab.CrankPdxImport");
            Budget=Class.forName("com.cranklab.CrankPdxImport$Budget");
            BudgetConstructor=Budget.getDeclaredConstructor(File.class); BudgetConstructor.setAccessible(true);
            Extract=Importer.getDeclaredMethod("extractZip",InputStream.class,File.class,Budget); Extract.setAccessible(true);
            Install=Importer.getDeclaredMethod("install",File.class,File.class); Install.setAccessible(true);
        } catch (Exception e) { throw new ExceptionInInitializerError(e); }
    }
    static File extract(File archive,File stage,File library) throws Exception {
        stage.mkdirs();
        try (InputStream in=new FileInputStream(archive)) { Extract.invoke(null,in,stage,BudgetConstructor.newInstance(stage)); }
        return (File)Install.invoke(null,stage,library);
    }
    static File zip(File root,String name,String[][] files) throws Exception {
        File out=new File(root,name+".zip");
        try (ZipOutputStream z=new ZipOutputStream(new FileOutputStream(out))) {
            for (String[] file:files) { z.putNextEntry(new ZipEntry(file[0])); z.write(file[1].getBytes(StandardCharsets.UTF_8)); z.closeEntry(); }
        }
        return out;
    }
    static void reject(File root,String label,String[][] files) throws Exception {
        try { extract(zip(root,label,files),new File(root,".import-"+label),new File(root,"library")); throw new AssertionError("Accepted invalid archive: "+label); }
        catch (InvocationTargetException expected) { if (!(expected.getCause() instanceof IOException)) throw expected; }
        System.out.println("REJECTED\t"+label);
    }
    public static void main(String[] args) throws Exception {
        File root=new File(args[0]); root.mkdirs(); File library=new File(root,"library"); library.mkdirs();
        for (int i=1;i<args.length;i++) {
            String key=new File(args[i]).getName().replace(".zip","");
            File installed=extract(new File(args[i]),new File(root,".import-"+key),library);
            if (!new File(installed,"pdxinfo").isFile()) throw new AssertionError("Metadata missing");
            File again=extract(new File(args[i]),new File(root,".import-repeat-"+key),library);
            if (!installed.equals(again)) throw new AssertionError("Repeated import changed identity");
            System.out.println("IMPORTED\t"+key+"\t"+installed.getAbsolutePath());
        }
        reject(root,"traversal",new String[][]{{"../escaped.txt","bad"}});
        reject(root,"absolute",new String[][]{{"/absolute.txt","bad"}});
        reject(root,"windows-absolute",new String[][]{{"C:/escaped.txt","bad"}});
        reject(root,"backslash",new String[][]{{"..\\escaped.txt","bad"}});
        reject(root,"invalid-game",new String[][]{{"readme.txt","hello"}});
        reject(root,"invalid-native",new String[][]{{"pdxinfo","name=C only"},{"pdex.bin","native"}});
        reject(root,"two-games",new String[][]{{"one/pdxinfo","name=one"},{"one/main.lua","playdate.update=function()end"},{"two/pdxinfo","name=two"},{"two/main.lua","playdate.update=function()end"}});
        File unicode=zip(root,"unicode",new String[][]{{"game.pdx/pdxinfo","name=中文游戏\nbundleID=com.cranklab.unicode"},{"game.pdx/main.lua","playdate.update=function()end"},{"game.pdx/res/音效.dat","\u0000binary"}});
        File installed=extract(unicode,new File(root,".import-unicode"),library);
        if (!new File(installed,"res/音效.dat").isFile()) throw new AssertionError("Unicode file name lost");
        byte[] bytes=Files.readAllBytes(new File(installed,"res/音效.dat").toPath());
        if (bytes[0]!=0 || bytes.length!=7) throw new AssertionError("Binary bytes changed");
        System.out.println("PASSED\tunicode-and-binary");
        if (new File(root.getParentFile(),"escaped.txt").exists()) throw new AssertionError("ZIP escaped staging root");
    }
}
