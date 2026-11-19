package kindleroid.koboprobe;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.graphics.*;
import android.view.View;
import java.io.*;
public final class ProbeActivity extends Activity {
 private static final String TAG="KoboBatchProbe";
 private boolean externalProbed;
 private synchronized void record(String result){
  Log.i(TAG,result);
  try{FileOutputStream out=new FileOutputStream(new File(getFilesDir(),"probe-results.txt"),true);out.write((result+"\n").getBytes("UTF-8"));out.close();}
  catch(IOException e){Log.e(TAG,"RESULT_CAPTURE_FAILED",e);}
 }
 private void resetResults(){
  try{FileOutputStream out=new FileOutputStream(new File(getFilesDir(),"probe-results.txt"),false);out.write(("RUN_ID="+getIntent().getStringExtra("run_id")+"\n").getBytes("UTF-8"));out.close();}
  catch(IOException e){Log.e(TAG,"RESULT_CAPTURE_FAILED",e);}
 }
 @Override public void onCreate(Bundle b){super.onCreate(b);resetResults();record("APP_ONCREATE=PASS pid="+android.os.Process.myPid());
  try{File f=new File(getFilesDir(),"roundtrip.txt");String v="kobo-batch-"+System.nanoTime();FileOutputStream o=new FileOutputStream(f);o.write(v.getBytes("UTF-8"));o.close();FileInputStream in=new FileInputStream(f);byte[] a=new byte[128];int n=in.read(a);in.close();if(!v.equals(new String(a,0,n,"UTF-8")))throw new IOException("mismatch");record("APP_INTERNAL_STORAGE=PASS");
   Bitmap bm=Bitmap.createBitmap(240,160,Bitmap.Config.ARGB_8888);Canvas c=new Canvas(bm);c.drawColor(Color.WHITE);Paint p=new Paint();p.setColor(Color.BLACK);c.drawRect(20,20,100,100,p);if(bm.getPixel(30,30)!=Color.BLACK||bm.getPixel(200,140)!=Color.WHITE)throw new IOException("pixels");FileOutputStream png=new FileOutputStream(new File(getFilesDir(),"render.png"));if(!bm.compress(Bitmap.CompressFormat.PNG,100,png))throw new IOException("png");png.close();bm.recycle();record("APP_BITMAP_PNG=PASS");
  }catch(Throwable t){Log.e(TAG,"APP_STORAGE_OR_BITMAP=FAIL",t);}
  setContentView(new View(this){boolean drawn;@Override protected void onDraw(Canvas c){super.onDraw(c);c.drawColor(Color.WHITE);Paint p=new Paint();p.setColor(Color.BLACK);p.setTextSize(28);c.drawText("Kobo Android probe",20,60,p);if(!drawn){drawn=true;record("APP_VIEW_DRAW=PASS width="+getWidth()+" height="+getHeight());}}});
 }
 private void probeExternalStorage(){new Thread(new Runnable(){public void run(){try{
  File dir=getExternalFilesDir(null);if(dir==null){record("APP_EXTERNAL_STORAGE=UNAVAILABLE");return;}
  File f=new File(dir,"roundtrip.txt");String v="external-"+System.nanoTime();FileOutputStream o=new FileOutputStream(f);o.write(v.getBytes("UTF-8"));o.close();FileInputStream in=new FileInputStream(f);byte[] a=new byte[128];int n=in.read(a);in.close();if(!v.equals(new String(a,0,n,"UTF-8")))throw new IOException("external mismatch");record("APP_EXTERNAL_STORAGE=PASS");
 }catch(Throwable t){record("APP_EXTERNAL_STORAGE=FAIL");Log.e(TAG,"EXTERNAL_STORAGE_DETAIL",t);}}},"kobo-storage-probe").start();}
 @Override public void onResume(){super.onResume();record("APP_ONRESUME=PASS");if(!externalProbed){externalProbed=true;probeExternalStorage();}}
}
