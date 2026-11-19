public final class KoboOfflineSyncProbe {
 public static void main(String[] args) throws Exception {
  Class<?> c=Class.forName("android.content.ContentResolver");
  String[] result=(String[])c.getMethod("getSyncAdapterPackagesForAuthorityAsUser",String.class,int.class).invoke(null,"settings",0);
  if(result==null||result.length!=0)throw new AssertionError("offline sync adapters");
  System.out.println("OFFLINE_SYNC_QUERY=PASS adapters=0");
 }
}
