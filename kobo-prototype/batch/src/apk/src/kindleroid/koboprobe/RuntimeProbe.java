package kindleroid.koboprobe;
import android.graphics.*;
import java.io.*;
public final class RuntimeProbe {
 public static void main(String[] args) throws Exception {
  Bitmap b=Bitmap.createBitmap(240,160,Bitmap.Config.ARGB_8888);Canvas c=new Canvas(b);c.drawColor(Color.WHITE);Paint p=new Paint();p.setColor(Color.BLACK);c.drawRect(10,10,80,80,p);p.setTextSize(22);c.drawText("Kobo",90,90,p);
  if(b.getPixel(20,20)!=Color.BLACK||b.getPixel(200,140)!=Color.WHITE)throw new IOException("pixel mismatch");
  File f=new File("/data/local/tmp/apk-runtime.png");FileOutputStream out=new FileOutputStream(f);if(!b.compress(Bitmap.CompressFormat.PNG,100,out))throw new IOException("png");out.close();b.recycle();
  Bitmap decoded=BitmapFactory.decodeFile(f.getPath());if(decoded==null||decoded.getWidth()!=240||decoded.getPixel(20,20)!=Color.BLACK)throw new IOException("png readback");decoded.recycle();
  System.out.println("APK_DEX_BITMAP_PNG=PASS file_bytes="+f.length());
 }
}
