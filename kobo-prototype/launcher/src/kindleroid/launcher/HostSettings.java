package kindleroid.launcher;
import android.content.Context;
import java.io.*;
import java.util.HashMap;
/** Fixed numeric protocol. The host validates key, value, file type, owner and size again. */
final class HostSettings {
    private static int serial=(int)(android.os.SystemClock.elapsedRealtime()%1000000000);
    final HashMap<String,String> values=new HashMap<String,String>();
    int get(String key,int fallback){try{return Integer.parseInt(values.get(key));}catch(Exception e){return fallback;}}
    static File directory(Context c){File ram=new File("/dev/kobodroid-ui/"+android.os.Process.myUid());return ram.isDirectory()&&ram.canWrite()?ram:c.getFilesDir();}
    boolean hasHostState(){return get("flash",-1)>=0&&get("idle",-1)>=0;}
    /** The root-owned RAM channel may be created after an Activity first resumes. */
    static final class Watch {
        private final Context context;private final Runnable change;
        private final android.os.Handler main=new android.os.Handler(android.os.Looper.getMainLooper());
        private android.os.FileObserver observer;private String watched;private volatile boolean active=true,invalidated;private long nextQuery;
        private final Runnable tick=new Runnable(){public void run(){check();}};
        Watch(Context c,Runnable callback){context=c.getApplicationContext();change=callback;check();}
        private void check(){
            if(!active)return;
            String path=directory(context).getAbsolutePath();
            boolean switched=watched!=null&&!path.equals(watched);
            if(observer==null||invalidated||!path.equals(watched)){
                if(observer!=null)observer.stopWatching();watched=path;
                invalidated=false;
                observer=new android.os.FileObserver(path,android.os.FileObserver.MOVED_TO|android.os.FileObserver.CLOSE_WRITE|android.os.FileObserver.DELETE_SELF|android.os.FileObserver.MOVE_SELF){
                    public void onEvent(int event,String name){boolean gone=(event&(android.os.FileObserver.DELETE_SELF|android.os.FileObserver.MOVE_SELF))!=0;if(gone)invalidated=true;if("ui.state".equals(name)||gone)main.post(tick);}
                };observer.startWatching();
            }
            if(switched){nextQuery=android.os.SystemClock.elapsedRealtime()+2000;try{request(context,"query",0);}catch(IOException ignored){}}
            change.run();main.removeCallbacks(tick);
            if(!read(context).hasHostState()){
                long now=android.os.SystemClock.elapsedRealtime();
                if(now>=nextQuery){nextQuery=now+2000;try{request(context,"query",0);}catch(IOException ignored){}}
                main.postDelayed(tick,1000);
            }
        }
        void stopWatching(){active=false;main.removeCallbacks(tick);if(observer!=null){observer.stopWatching();observer=null;}}
    }
    static Watch observe(Context c,Runnable change){return new Watch(c,change);}
    boolean connected(){return get("wifi",-1)==1;}
    static HostSettings read(Context c){
        HostSettings result=new HostSettings();File f=new File(directory(c),"ui.state");
        if(f.length()>2048)return result;
        try { BufferedReader in=new BufferedReader(new FileReader(f));try{String line;while((line=in.readLine())!=null){String[] a=line.trim().split(" +");for(String word:a){int p=word.indexOf('=');if(p>0)result.values.put(word.substring(0,p),word.substring(p+1));}}}finally{in.close();}}catch(IOException e){}return result;
    }
    static int request(Context c,String key,int value)throws IOException{
        int max=(key.equals("flash")||key.equals("idle"))?3:(key.equals("cc")||key.equals("exit")||key.equals("wifi")||key.equals("pagewake"))?1:key.equals("query")?0:-1;
        if(max<0||value<0||value>max)throw new IOException("invalid setting");
        int id=++serial;atomic(c,"ui.request",(id+" "+key+" "+value+"\n").getBytes("US-ASCII"));return id;

    }
    static void atomic(Context c,String name,byte[] bytes)throws IOException{
        File dir=directory(c);File f=File.createTempFile("kd-request-",".tmp",dir);
        try{FileOutputStream out=new FileOutputStream(f);try{out.write(bytes);out.getFD().sync();}finally{out.close();}
            if(!f.renameTo(new File(dir,name)))throw new IOException("request rename failed");
        }finally{f.delete();}
    }
}
