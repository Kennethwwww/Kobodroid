public final class KoboReady {
 public static void main(String[] args) {
  try {
   Class<?> c=Class.forName("android.app.ActivityManagerNative");
   boolean ready=(Boolean)c.getMethod("isSystemReady").invoke(null);
   if(ready){System.out.println("ACTIVITY_MANAGER_SYSTEM_READY=PASS");return;}
  } catch(Throwable unavailable) { }
  System.exit(20);
 }
}
