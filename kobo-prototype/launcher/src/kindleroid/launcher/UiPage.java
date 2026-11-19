package kindleroid.launcher;
import android.app.Activity;
import android.content.*;
import android.graphics.Color;
import android.os.*;
import android.view.*;
import android.widget.*;
import android.util.TypedValue;
import java.text.SimpleDateFormat;
import java.util.*;
/** Common physical-pixel page chrome. No ripple, shadow, animator or dp/sp assumptions. */
public class UiPage extends Activity {
    LinearLayout root,body,footer;
    private TextView statusClock,statusPower;private ImageView statusNetwork;private boolean listening;private HostSettings.Watch hostStateObserver;
    private final BroadcastReceiver chromeChanged=new BroadcastReceiver(){public void onReceive(Context c,Intent i){updateChrome(i);}};
    protected void updateChrome(Intent battery){
        if(statusClock==null)return;String time=new SimpleDateFormat("HH:mm",Locale.CHINA).format(new Date());if(!time.contentEquals(statusClock.getText()))statusClock.setText(time);
        int visible=HostSettings.read(this).connected()?View.VISIBLE:View.GONE;if(statusNetwork.getVisibility()!=visible)statusNetwork.setVisibility(visible);
        if(battery!=null&&Intent.ACTION_BATTERY_CHANGED.equals(battery.getAction())){int b=battery.getIntExtra("level",-1),scale=battery.getIntExtra("scale",100);if(b>=0&&scale>0){int n=b*100/scale;String label=n+"%";if(!label.contentEquals(statusPower.getText()))statusPower.setText(label);statusPower.setCompoundDrawables(null,null,UiStyle.battery(this,n,battery.getIntExtra("status",0)==2||battery.getIntExtra("plugged",0)!=0),null);}}
    }
    @Override protected void onResume(){super.onResume();IntentFilter f=new IntentFilter(Intent.ACTION_BATTERY_CHANGED);f.addAction(Intent.ACTION_TIME_TICK);f.addAction(Intent.ACTION_TIME_CHANGED);updateChrome(registerReceiver(chromeChanged,f));listening=true;hostStateObserver=HostSettings.observe(this,new Runnable(){public void run(){if(listening){updateChrome(null);onHostStateChanged();}}});try{HostSettings.request(this,"query",0);}catch(java.io.IOException e){}}
    protected void onHostStateChanged(){}
    @Override protected void onPause(){if(hostStateObserver!=null){hostStateObserver.stopWatching();hostStateObserver=null;}if(listening){unregisterReceiver(chromeChanged);listening=false;}super.onPause();}

    int px(int n){return UiStyle.px(this,n);}
    TextView text(String value,int size){TextView t=new TextView(this);t.setText(value);t.setTextColor(0xff000000);t.setTextSize(TypedValue.COMPLEX_UNIT_PX,px(size));t.setIncludeFontPadding(false);return t;}
    LinearLayout row(){LinearLayout v=new LinearLayout(this);v.setGravity(Gravity.CENTER_VERTICAL);return v;}
    LinearLayout.LayoutParams lp(int w,int h){return new LinearLayout.LayoutParams(w<0?w:px(w),h<0?h:px(h));}
    void rule(LinearLayout parent){View v=new View(this);v.setBackgroundColor(0xffbbbbbb);parent.addView(v,lp(-1,1));}
    void page(String title){
        getWindow().setWindowAnimations(0);
        root=new LinearLayout(this);root.setOrientation(1);root.setBackgroundColor(Color.WHITE);
        LinearLayout bar=row();bar.setPadding(px(12),0,px(12),0);View back=UiStyle.iconButton(this,"nav_back","返回上一级");back.setOnClickListener(new View.OnClickListener(){public void onClick(View v){finish();}});bar.addView(back,lp(48,48));TextView name=text(title,20);name.setTypeface(android.graphics.Typeface.DEFAULT,android.graphics.Typeface.BOLD);LinearLayout.LayoutParams np=lp(0,-1);np.weight=1;np.leftMargin=px(16);name.setGravity(Gravity.CENTER_VERTICAL);bar.addView(name,np);root.addView(bar,lp(-1,80));
        ScrollView scroll=new ScrollView(this);scroll.setOverScrollMode(View.OVER_SCROLL_NEVER);body=new LinearLayout(this);body.setOrientation(1);body.setPadding(px(20),0,px(20),px(6));scroll.addView(body);root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        footer=row();footer.setGravity(Gravity.CENTER);footer.setPadding(px(32),px(16),px(32),px(28));root.addView(footer,lp(-1,-2));setContentView(root);
    }
    Button action(String label,boolean primary,View.OnClickListener click){Button b=new Button(this);b.setText(label);b.setTextSize(TypedValue.COMPLEX_UNIT_PX,px(19));b.setTextColor(primary?Color.WHITE:0xff000000);b.setAllCaps(false);b.setTypeface(android.graphics.Typeface.create("sans-serif",android.graphics.Typeface.NORMAL));b.setIncludeFontPadding(false);b.setStateListAnimator(null);b.setMinimumHeight(0);b.setMinHeight(0);b.setMinimumWidth(0);b.setPadding(px(20),0,px(20),0);android.graphics.drawable.GradientDrawable bg=UiStyle.surface(primary?Color.BLACK:Color.WHITE,0,this);if(!primary)bg.setStroke(px(1),Color.BLACK);b.setBackground(bg);b.setOnClickListener(click);return b;}
    void bottom(String label,View.OnClickListener click){footer.addView(action(label,true,click),lp(220,54));}
    void section(String label){TextView t=text(label,19);t.setTextColor(0xff333333);LinearLayout.LayoutParams p=lp(-1,28);if(body.getChildCount()>0)p.topMargin=px(8);body.addView(t,p);}
    @Override public void finish(){super.finish();overridePendingTransition(0,0);}
}
