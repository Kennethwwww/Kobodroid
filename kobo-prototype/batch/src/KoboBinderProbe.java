import android.os.*;
import java.io.*;
public final class KoboBinderProbe {
 public static void main(String[] args) throws Exception {
  Class<?> sm=Class.forName("android.os.ServiceManager");
  IBinder remote=(IBinder)sm.getMethod("checkService",String.class).invoke(null,"kd.echo");
  if(remote==null||!remote.pingBinder())throw new IOException("service unavailable");
  Parcel a=Parcel.obtain(),r=Parcel.obtain();a.writeInt(41);a.writeString("kobo-java");
  if(!remote.transact(1,a,r,0)||r.readInt()!=42||!"kobo-java".equals(r.readString()))throw new IOException("echo");a.recycle();r.recycle();System.out.println("JAVA_BINDER_ECHO=PASS");
  Binder cb=new Binder(){@Override protected boolean onTransact(int code,Parcel in,Parcel out,int flags){if(code!=100)return false;out.writeInt(in.readInt()*2);return true;}};
  a=Parcel.obtain();r=Parcel.obtain();a.writeStrongBinder(cb);a.writeInt(21);
  if(!remote.transact(2,a,r,0)||r.readInt()!=0||r.readInt()!=42)throw new IOException("callback");a.recycle();r.recycle();System.out.println("JAVA_BINDER_CALLBACK=PASS");
  File f=new File("/data/local/tmp/java-binder-fd");FileOutputStream out=new FileOutputStream(f);out.write("fd-ok".getBytes("UTF-8"));out.close();ParcelFileDescriptor fd=ParcelFileDescriptor.open(f,ParcelFileDescriptor.MODE_READ_ONLY);
  a=Parcel.obtain();r=Parcel.obtain();a.writeFileDescriptor(fd.getFileDescriptor());if(!remote.transact(3,a,r,0)||r.readInt()!=5||!"fd-ok".equals(r.readString()))throw new IOException("fd");a.recycle();r.recycle();fd.close();f.delete();System.out.println("JAVA_BINDER_FD=PASS");
  System.out.println("JAVA_BINDER_RESULT=PASS");
 }
}
