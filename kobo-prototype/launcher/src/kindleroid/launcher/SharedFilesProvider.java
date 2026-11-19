package kindleroid.launcher;
import android.database.Cursor;import android.database.MatrixCursor;import android.os.CancellationSignal;import android.os.ParcelFileDescriptor;import android.provider.DocumentsContract.Document;import android.provider.DocumentsContract.Root;import android.provider.DocumentsProvider;import android.webkit.MimeTypeMap;
import java.io.File;import java.io.FileNotFoundException;import java.io.IOException;import java.util.Locale;
/** Read-only SAF access to the Kobo's own storage (books and documents). No storage permission or worker. */
public final class SharedFilesProvider extends DocumentsProvider {
 private static final String PREFIX="books:";private static final File BASE=new File("/dev/kobodroid-books");
 private static final String[] ROOTS={Root.COLUMN_ROOT_ID,Root.COLUMN_TITLE,Root.COLUMN_FLAGS,Root.COLUMN_DOCUMENT_ID,Root.COLUMN_MIME_TYPES,Root.COLUMN_ICON};
 private static final String[] DOCS={Document.COLUMN_DOCUMENT_ID,Document.COLUMN_DISPLAY_NAME,Document.COLUMN_MIME_TYPE,Document.COLUMN_FLAGS,Document.COLUMN_SIZE,Document.COLUMN_LAST_MODIFIED};
 public boolean onCreate(){return true;}
 private static boolean hidden(String relative) {
  for (String part : relative.split("/")) if (part.startsWith(".")) return true;
  return false;
 }
 private File file(String id)throws FileNotFoundException{
  if(id==null||!id.startsWith(PREFIX))throw new FileNotFoundException("invalid document");String relative=id.substring(PREFIX.length());
  if(relative.startsWith("/")||hidden(relative)||relative.indexOf('\0')>=0)throw new FileNotFoundException("invalid document");
  try{File base=BASE.getCanonicalFile(),f=new File(base,relative).getCanonicalFile();if(!f.equals(base)&&!f.getPath().startsWith(base.getPath()+"/"))throw new FileNotFoundException("outside shared folder");if(!f.equals(base)&&hidden(f.getPath().substring(base.getPath().length()+1)))throw new FileNotFoundException("hidden document");if(!f.exists())throw new FileNotFoundException("missing document");return f;}catch(IOException e){throw new FileNotFoundException("unavailable document");}
 }
 private String id(File f)throws IOException{String base=BASE.getCanonicalPath(),path=f.getCanonicalPath();if(path.equals(base))return PREFIX;if(!path.startsWith(base+"/"))throw new IOException("outside shared folder");return PREFIX+path.substring(base.length()+1);}
 private static String type(File f){if(f.isDirectory())return Document.MIME_TYPE_DIR;String name=f.getName();int dot=name.lastIndexOf('.');String ext=dot<0?"":name.substring(dot+1).toLowerCase(Locale.US);if("json".equals(ext))return "application/json";String value=MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);return value==null?"application/octet-stream":value;}
 public Cursor queryRoots(String[] projection){MatrixCursor c=new MatrixCursor(projection==null?ROOTS:projection);if(BASE.isDirectory())row(c,new Object[][]{{Root.COLUMN_ROOT_ID,"kobo-files"},{Root.COLUMN_TITLE,"Kobo 文件"},{Root.COLUMN_FLAGS,Root.FLAG_LOCAL_ONLY},{Root.COLUMN_DOCUMENT_ID,PREFIX},{Root.COLUMN_MIME_TYPES,"*/*"},{Root.COLUMN_ICON,R.drawable.ic_files}});return c;}
 private static void row(MatrixCursor c,Object[][] values){MatrixCursor.RowBuilder row=c.newRow();for(String column:c.getColumnNames()){Object value=null;for(Object[] pair:values)if(column.equals(pair[0])){value=pair[1];break;}row.add(value);}}
 private void document(MatrixCursor c,File f)throws FileNotFoundException{try{row(c,new Object[][]{{Document.COLUMN_DOCUMENT_ID,id(f)},{Document.COLUMN_DISPLAY_NAME,f.equals(BASE.getCanonicalFile())?"Kobo 文件":f.getName()},{Document.COLUMN_MIME_TYPE,type(f)},{Document.COLUMN_FLAGS,0},{Document.COLUMN_SIZE,f.isFile()?f.length():null},{Document.COLUMN_LAST_MODIFIED,f.lastModified()}});}catch(IOException e){throw new FileNotFoundException("unavailable document");}}
 public Cursor queryDocument(String documentId,String[] projection)throws FileNotFoundException{MatrixCursor c=new MatrixCursor(projection==null?DOCS:projection);document(c,file(documentId));return c;}
 public Cursor queryChildDocuments(String parentId,String[] projection,String order)throws FileNotFoundException{File parent=file(parentId);if(!parent.isDirectory())throw new FileNotFoundException("not a directory");MatrixCursor c=new MatrixCursor(projection==null?DOCS:projection);File[] children=parent.listFiles();if(children!=null)for(File child:children)try{document(c,file(id(child)));}catch(IOException ignored){}return c;}
 public ParcelFileDescriptor openDocument(String documentId,String mode,CancellationSignal signal)throws FileNotFoundException{if(!"r".equals(mode))throw new FileNotFoundException("read only");if(signal!=null)signal.throwIfCanceled();File f=file(documentId);if(!f.isFile())throw new FileNotFoundException("not a file");return ParcelFileDescriptor.open(f,ParcelFileDescriptor.MODE_READ_ONLY);}
 public boolean isChildDocument(String parentId,String childId){try{return file(childId).getPath().startsWith(file(parentId).getPath()+"/");}catch(FileNotFoundException e){return false;}}
}
