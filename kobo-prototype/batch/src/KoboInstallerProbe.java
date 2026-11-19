import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import java.io.DataInputStream;
import java.io.OutputStream;

/** The same Java reserved-socket path used by InstallerConnection, read-only ping. */
public final class KoboInstallerProbe {
 private static boolean ping(String identity) {
  LocalSocket s=new LocalSocket();
  try {
   s.connect(new LocalSocketAddress("installd",LocalSocketAddress.Namespace.RESERVED));
   s.setSoTimeout(3000);
   OutputStream out=s.getOutputStream();
   out.write(new byte[]{4,0,'p','i','n','g'});out.flush();
   DataInputStream in=new DataInputStream(s.getInputStream());
   int n=in.readUnsignedByte()|(in.readUnsignedByte()<<8);
   if(n<1||n>256)throw new Exception("invalid reply length="+n);
   byte[] b=new byte[n];in.readFully(b);
   String reply=new String(b,"UTF-8");
   System.out.println("JAVA_INSTALLER_PING identity="+identity+" reply="+reply);
   return "0".equals(reply);
  } catch(Exception e) {
   System.out.println("JAVA_INSTALLER_PING identity="+identity+" result=FAIL");
   e.printStackTrace(System.out);return false;
  } finally {try{s.close();}catch(Exception ignored){}}
 }
 public static void main(String[] args) {
  String identity=args.length==1?args[0]:"root";
  boolean ok=ping(identity);
  System.out.println("JAVA_INSTALLER_RESULT identity="+identity+" result="+(ok?"PASS":"FAIL"));
  if(!ok)System.exit(1);
 }
}
