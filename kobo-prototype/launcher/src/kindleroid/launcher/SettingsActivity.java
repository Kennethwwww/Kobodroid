package kindleroid.launcher;
import android.app.*;
import android.os.*;
import android.content.*;
import android.graphics.*;
import android.view.*;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.*;
import java.io.*;
/** Unified UI, host values are confirmed by the fixed numeric channel (never optimistic saved values). */
public class SettingsActivity extends UiPage {
    private android.content.SharedPreferences prefs;
    private String choiceKey;private String[] choiceLabels;private final java.util.ArrayList<ChoiceMark> radios=new java.util.ArrayList<ChoiceMark>();
    private TextView wifiValue,flashValue,idleValue,columnsValue,pageWakeValue;
    private final Handler poll=new Handler();private String pending;private int wanted,pendingId;private long deadline;private boolean active;private int queries;
    private HostSettings host=new HostSettings();
    private final Runnable update=new Runnable(){public void run(){if(!active)return;if(++queries%8==0&&pending==null)try{HostSettings.request(SettingsActivity.this,"query",0);}catch(IOException e){}host=HostSettings.read(SettingsActivity.this);if(pending!=null){if(host.get(pending+"_ack",-1)==pendingId&&host.get(pending+"_error",-1)>0){String failed=pending;pending=null;failedSetting(failed);}else if(host.get(pending+"_ack",-1)==pendingId&&host.get(pending+"_error",-1)==0&&host.get(pending,-1)==wanted){String saved=pending;pending=null;android.util.Log.i("KobodroidLauncher","HOST_SETTING_CONFIRMED");if(choiceKey!=null){finish();return;}}else if(SystemClock.elapsedRealtime()>deadline){String failed=pending;pending=null;failedSetting(failed);}}renderValues();if(pending!=null)poll.postDelayed(this,500);}};
    @Override protected void onCreate(Bundle state){super.onCreate(state);prefs=Prefs.get(this);String screen=getIntent().getStringExtra("screen");
        if("choice".equals(screen)){choiceKey=getIntent().getStringExtra("key");choiceLabels=getIntent().getStringArrayExtra("labels");if(choiceKey==null||choiceLabels==null){finish();return;}page(getIntent().getStringExtra("title"));host=HostSettings.read(this);for(int i=0;i<choiceLabels.length;i++){final int value=i;LinearLayout r=row();r.setPadding(px(16),0,px(16),0);ChoiceMark radio=new ChoiceMark(i==choiceValue());radios.add(radio);r.addView(radio,lp(36,36));TextView label=text(choiceLabels[i],18);LinearLayout.LayoutParams l=lp(0,-2);l.weight=1;l.leftMargin=px(12);r.addView(label,l);r.setOnClickListener(new View.OnClickListener(){public void onClick(View v){if(pending!=null)return;if("columns".equals(choiceKey)){prefs.edit().putInt(Prefs.COLUMNS,value+3).apply();finish();return;}if(choiceValue()==value){finish();return;}save(choiceKey,value);}});r.setFocusable(true);r.setContentDescription(choiceLabels[i]);body.addView(r,lp(-1,68));rule(body);}}
        else if("display".equals(screen)){page("屏幕");
            flashValue=setting("全屏刷新",new View.OnClickListener(){public void onClick(View v){choices("全屏刷新","flash",FLASH_LABELS);}});
            idleValue=setting("自动休眠",new View.OnClickListener(){public void onClick(View v){choices("自动休眠","idle",IDLE_LABELS);}});
            setting("休眠壁纸","moon",new View.OnClickListener(){public void onClick(View v){wallpaperMenu();}});
            pageWakeValue=setting("翻页键唤醒",new View.OnClickListener(){public void onClick(View v){
                int enabled=host.get("pagewake",-1);
                if(pending==null&&enabled>=0)save("pagewake",1-enabled);
            }});
        }else if("home".equals(screen)){page("桌面");
            columnsValue=setting("每行应用数",new View.OnClickListener(){public void onClick(View v){choices("每行应用数","columns",new String[]{"3","4","5"});}});columnsValue.setText(""+Prefs.columns(this));
            toggle("左右滑动翻页",Prefs.SWIPE_PAGING,true);toggle("显示隐藏的应用",Prefs.SHOW_HIDDEN,false);
        }else if("storage".equals(screen)){page("应用与存储");
            final android.os.StatFs fs=new android.os.StatFs(android.os.Environment.getDataDirectory().getAbsolutePath());
            TextView capacity=text("总容量 "+gib(fs.getTotalBytes())+" · 可用 "+gib(fs.getAvailableBytes()),19);body.addView(capacity,lp(-1,60));rule(body);
            setting("应用安装",new View.OnClickListener(){public void onClick(View v){startActivity(new Intent(SettingsActivity.this,InstallActivity.class));}});
        }else{page("设置");
            wifiValue=setting("无线网络","wifi",new View.OnClickListener(){public void onClick(View v){if(pending!=null||host.get("wifi",-1)<0||host.get("wifi_busy",0)==1)return;save("wifi",host.connected()?0:1);}});
            destination("屏幕","display","sun");destination("桌面","home","home");destination("应用与存储","storage","folder");
            setting("手势","gesture",new View.OnClickListener(){public void onClick(View v){startActivity(new Intent(SettingsActivity.this,GuideActivity.class));}});
            setting("关于","info",new View.OnClickListener(){public void onClick(View v){about();}});
            setting("返回 Kobo","exit",new View.OnClickListener(){public void onClick(View v){exit();}});
        }renderValues();
    }
    private static final String[] FLASH_LABELS={"不全刷","每 3 页","每 6 页","每 12 页"},IDLE_LABELS={"5 分钟","15 分钟","30 分钟","从不"};
    private void destination(String label,final String screen,String icon){setting(label,icon,new View.OnClickListener(){public void onClick(View v){startActivity(new Intent(SettingsActivity.this,SettingsActivity.class).putExtra("screen",screen));}});}
    private String gib(long n){return String.format(java.util.Locale.CHINA,"%.1f GB",n/(1024d*1024*1024));}
    private void exit(){try{HostSettings.request(this,"exit",1);}catch(IOException e){android.util.Log.e("KobodroidLauncher","EXIT_REQUEST_FAILED",e);Toast.makeText(this,"未能返回，请重试",Toast.LENGTH_LONG).show();}}
    private int choiceValue(){return "columns".equals(choiceKey)?Prefs.columns(this)-3:host.get(choiceKey,-1);}
    private LinearLayout baseRow(String label, String icon) {
        LinearLayout r = row();
        r.setPadding(px(16), 0, px(16), 0);
        if (icon != null) {
            ImageView badge = new ImageView(this);
            badge.setImageDrawable(UiStyle.icon(this, icon));
            badge.setScaleType(ImageView.ScaleType.FIT_CENTER);
            badge.setPadding(px(8), px(8), px(8), px(8));
            badge.setBackground(UiStyle.surface(UiStyle.CONTROL_FILL, 18, this));
            LinearLayout.LayoutParams slot = lp(36, 36);
            slot.rightMargin = px(14);
            r.addView(badge, slot);
        }
        r.addView(text(label, 18), new LinearLayout.LayoutParams(0, -2, 1));
        return r;
    }
    private TextView setting(String label, View.OnClickListener click) {
        return setting(label, null, click);
    }
    private TextView setting(String label, String icon, View.OnClickListener click) {
        LinearLayout r = baseRow(label, icon);
        TextView value = text("", 18);
        value.setTextColor(0xff333333);
        r.addView(value);
        ImageView arrow = new ImageView(this);
        arrow.setImageDrawable(UiStyle.icon(this, "next"));
        LinearLayout.LayoutParams slot = lp(20, 20);
        slot.leftMargin = px(8);
        r.addView(arrow, slot);
        r.setOnClickListener(click);
        r.setFocusable(true);
        body.addView(r, lp(-1, 68));
        rule(body);
        return value;
    }
    private final class ChoiceMark extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final android.graphics.drawable.Drawable check = UiStyle.icon(SettingsActivity.this, "check").mutate();
        private boolean checked;
        ChoiceMark(boolean value) {
            super(SettingsActivity.this);
            checked = value;
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        }
        boolean isChecked() { return checked; }
        void setChecked(boolean value) { checked = value; invalidate(); }
        @Override protected void onDraw(Canvas canvas) {
            float x = getWidth() / 2f, y = getHeight() / 2f;
            paint.setColor(checked ? Color.BLACK : UiStyle.CONTROL_FILL);
            canvas.drawCircle(x, y, px(14), paint);
            if (checked) UiStyle.drawIcon(canvas, check, x, y, px(18), Color.WHITE);
        }
        @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) {
            super.onInitializeAccessibilityNodeInfo(info);
            info.setClassName("android.widget.RadioButton");
            info.setCheckable(true);
            info.setChecked(checked);
        }
    }
    private void toggle(String label,final String key,boolean def){final LinearLayout r=baseRow(label,null);final Toggle sw=new Toggle(prefs.getBoolean(key,def));r.addView(sw,lp(54,32));r.setOnClickListener(new View.OnClickListener(){public void onClick(View v){sw.on=!sw.on;prefs.edit().putBoolean(key,sw.on).apply();sw.invalidate();sw.sendAccessibilityEvent(android.view.accessibility.AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);}});r.setFocusable(true);r.setContentDescription(label);body.addView(r,lp(-1,68));rule(body);}
    private class Toggle extends View {boolean on;Paint p=new Paint(3);Toggle(boolean on){super(SettingsActivity.this);this.on=on;setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);}protected void onDraw(Canvas c){float s=getWidth()/54f;c.save();c.scale(s,s);p.setStyle(Paint.Style.FILL);p.setColor(on?Color.BLACK:UiStyle.CONTROL_FILL);c.drawRoundRect(new RectF(.5f,.5f,53.5f,31.5f),16,16,p);p.setStyle(Paint.Style.FILL);p.setColor(on?Color.WHITE:UiStyle.CONTROL_MUTED);c.drawCircle(on?38:16,16,12,p);c.restore();}public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo i){super.onInitializeAccessibilityNodeInfo(i);i.setClassName("android.widget.Switch");i.setCheckable(true);i.setChecked(on);}}
    private static final int PICK_SLEEP_WALLPAPER = 41;
    private void wallpaperMenu() {
        UiStyle.showFlat(new AlertDialog.Builder(this).setTitle("休眠壁纸")
            .setItems(new String[]{"选择 PNG", "恢复默认"}, (dialog, which) -> {
                if (which == 1) { updateWallpaper(null); return; }
                try {
                    startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
                            .addCategory(Intent.CATEGORY_OPENABLE).setType("image/png"), PICK_SLEEP_WALLPAPER);
                } catch (ActivityNotFoundException e) {
                    Toast.makeText(this, "文件选择器不可用", Toast.LENGTH_SHORT).show();
                }
            }).create());
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_SLEEP_WALLPAPER && result == RESULT_OK && data != null && data.getData() != null)
            updateWallpaper(data.getData());
    }
    private void updateWallpaper(final android.net.Uri uri) {
        new Thread(() -> {
            boolean ok;
            try {
                if (uri == null) SleepWallpaper.reset(this);
                else SleepWallpaper.select(this, uri);
                ok = true;
            } catch (IOException | RuntimeException e) {
                android.util.Log.w("KobodroidLauncher", "SLEEP_WALLPAPER_FAILED", e);
                ok = false;
            }
            final boolean success = ok;
            runOnUiThread(() -> Toast.makeText(this, success ? "已保存" : "无法保存壁纸", Toast.LENGTH_SHORT).show());
        }, "sleep-wallpaper").start();
    }
    private void renderValues(){updateChrome(null);setValue(columnsValue,""+Prefs.columns(this));if(choiceKey!=null){for(int i=0;i<radios.size();i++){boolean checked=choiceValue()==i;if(radios.get(i).isChecked()!=checked)radios.get(i).setChecked(checked);}}
        setValue(wifiValue,"wifi".equals(pending)||host.get("wifi_busy",0)==1?"正在切换…":host.get("wifi",-1)<0?"暂不可用":host.connected()?"已连接":"已关闭");
        setValue(flashValue,value("flash",FLASH_LABELS));setValue(idleValue,value("idle",IDLE_LABELS));setValue(pageWakeValue,value("pagewake",new String[]{"关闭","开启 · 浅眠"}));}
    private void setValue(TextView t,String value){if(t!=null&&!android.text.TextUtils.equals(t.getText(),value))t.setText(value);}
    private String value(String key,String[] labels){if(key.equals(pending))return "正在保存…";int n=host.get(key,-1);return n>=0&&n<labels.length?labels[n]:"暂不可用";}
    private void failedSetting(String key){Toast.makeText(this,"wifi".equals(key)?"无线网络未连接，请重试":"未能保存，请重试",Toast.LENGTH_SHORT).show();}
    private void save(String key,int value){try{pendingId=HostSettings.request(this,key,value);pending=key;wanted=value;deadline=SystemClock.elapsedRealtime()+("wifi".equals(key)?40000:12000);renderValues();poll.removeCallbacks(update);poll.postDelayed(update,250);}catch(IOException e){Toast.makeText(this,"请求失败，请重试",Toast.LENGTH_SHORT).show();}}
    private void choices(String title,final String key,String[] labels){if(pending!=null)return;if(!"columns".equals(key)&&host.get(key,-1)<0){Toast.makeText(this,"正在读取设置，请稍后重试",Toast.LENGTH_SHORT).show();return;}startActivity(new Intent(this,SettingsActivity.class).putExtra("screen","choice").putExtra("key",key).putExtra("title",title).putExtra("labels",labels));}

    private void info(String title,String message){UiStyle.showFlat(new AlertDialog.Builder(this).setTitle(title).setMessage(message).setPositiveButton("完成",null).create());}
    private void about(){UiStyle.showFlat(new AlertDialog.Builder(this).setTitle("Kobodroid").setItems(new String[]{"关于","Android 系统设置（高级）"},new DialogInterface.OnClickListener(){public void onClick(DialogInterface d,int which){if(which==1)startActivity(new Intent(android.provider.Settings.ACTION_SETTINGS));else info("关于 Kobodroid","在 Kobo 上运行的 Android 7。阅读时从顶边下滑打开控制中心，调节亮度、自然光和刷新；长按应用可查看信息、卸载或隐藏。");}}).create());}
    @Override protected void onHostStateChanged(){host=HostSettings.read(this);renderValues();}
    @Override protected void onResume(){super.onResume();active=true;try{HostSettings.request(this,"query",0);}catch(IOException e){}poll.post(update);}
    @Override protected void onPause(){active=false;poll.removeCallbacks(update);super.onPause();}
}
