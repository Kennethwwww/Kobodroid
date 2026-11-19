package kindleroid.launcher;
import android.os.Bundle;import android.view.*;import android.widget.*;
public class GuideActivity extends UiPage {
 protected void onCreate(Bundle b){super.onCreate(b);page("手势");body.setPadding(px(48),px(38),px(48),px(38));String[] titles={"返回","桌面","控制中心"},descs={"左右边缘向内滑","底边向上滑","顶边向下滑"},icons={"gesture_back","gesture_home","gesture_control"};
 for(int i=0;i<3;i++){LinearLayout r=row();r.setPadding(0,px(22),0,px(22));ImageView im=new ImageView(this);im.setImageDrawable(UiStyle.icon(this,icons[i]));im.setScaleType(ImageView.ScaleType.FIT_CENTER);r.addView(im,lp(86,110));LinearLayout words=new LinearLayout(this);words.setOrientation(1);words.setPadding(px(36),0,0,0);words.addView(text(titles[i],26));TextView description=text(descs[i],19);description.setTextColor(0xff333333);LinearLayout.LayoutParams p=lp(-1,-2);p.topMargin=px(8);words.addView(description,p);r.addView(words,new LinearLayout.LayoutParams(0,-2,1));body.addView(r,lp(-1,166));}
 bottom("知道了",new View.OnClickListener(){public void onClick(View v){finish();}});
 }
}
