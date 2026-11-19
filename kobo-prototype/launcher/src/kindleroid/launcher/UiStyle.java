package kindleroid.launcher;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.drawable.GradientDrawable;
import android.view.View;

/** Short-edge scaling from the approved 636 x 848 design canvas; no dp/sp density assumptions. */
final class UiStyle {
    private UiStyle() {}
    // E-ink dialogs change only their own rectangle; no full-screen dim layer or animation.
    static void showFlat(android.app.Dialog d) {
        noDim(d.getWindow()); d.show(); noDim(d.getWindow());
    }
    static void noDim(android.view.Window w) {
        if (w == null) return;
        w.clearFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        w.setDimAmount(0f); w.setWindowAnimations(0); w.getDecorView().setElevation(0f);
    }
    static int shortEdge(Context c) {
        android.util.DisplayMetrics metrics = c.getResources().getDisplayMetrics();
        return Math.min(metrics.widthPixels, metrics.heightPixels);
    }
    static int px(Context c, int logical) {
        return Math.max(logical == 0 ? 0 : 1, Math.round(logical * shortEdge(c) / 636f));
    }
    static GradientDrawable surface(int color, int radius, Context c) {
        GradientDrawable d = new GradientDrawable(); d.setColor(color); d.setCornerRadius(px(c, radius)); return d;
    }
    /* No pressed state anywhere: on e-ink a highlight costs a refresh on press and another on release, before the
       real change can show, and only adds lag. */
    static android.graphics.drawable.Drawable pressedOutline(Context c){return surface(Color.TRANSPARENT,0,c);}
    static android.graphics.drawable.Drawable battery(final Context c,final int percent,final boolean charging) {
        android.graphics.drawable.Drawable d=new android.graphics.drawable.Drawable(){
            final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
            @Override public void draw(Canvas canvas){
                canvas.save();canvas.translate(getBounds().left,getBounds().top);canvas.scale(getBounds().width()/24f,getBounds().height()/16f);
                p.setColor(Color.BLACK);p.setStyle(Paint.Style.STROKE);p.setStrokeWidth(1.3f);canvas.drawRoundRect(new RectF(1,3,21,13),1,1,p);
                p.setStyle(Paint.Style.FILL);canvas.drawRect(22,6,24,10,p);canvas.drawRect(3,5,3+16*Math.max(0,Math.min(100,percent))/100f,11,p);
                if(charging){p.setColor(percent>=50?Color.WHITE:Color.BLACK);Path b=new Path();b.moveTo(12,4);b.lineTo(8,8.5f);b.lineTo(11,8.5f);b.lineTo(10,12);b.lineTo(15,7);b.lineTo(12,7);b.close();canvas.drawPath(b,p);}
                canvas.restore();
            }
            @Override public void setAlpha(int alpha){p.setAlpha(alpha);invalidateSelf();}
            @Override public void setColorFilter(android.graphics.ColorFilter f){p.setColorFilter(f);invalidateSelf();}
            @Override public int getOpacity(){return android.graphics.PixelFormat.TRANSLUCENT;}
        };
        d.setBounds(0,0,px(c,24),px(c,16));return d;
    }
    static android.graphics.drawable.Drawable icon(Context c,String name) {
        int id=c.getResources().getIdentifier("ic_"+name,"drawable",c.getPackageName());
        return c.getResources().getDrawable(id,null);
    }
    static final int CONTROL_FILL = 0xffeeeeee;
    static final int CONTROL_MUTED = 0xff777777;

    static void drawIcon(Canvas canvas, android.graphics.drawable.Drawable icon,
                         float cx, float cy, float size, int color) {
        int left = Math.round(cx - size / 2), top = Math.round(cy - size / 2);
        int extent = Math.round(size);
        icon.setBounds(left, top, left + extent, top + extent);
        icon.setTint(color);
        icon.draw(canvas);
    }

    static View settingsButton(final Context c) { return iconButton(c,"settings","设置"); }
    static android.widget.ImageView iconButton(final Context c,String icon,String label) {
        android.widget.ImageView v=new android.widget.ImageView(c);
        v.setImageDrawable(icon(c,icon));v.setScaleType(android.widget.ImageView.ScaleType.FIT_CENTER);
        v.setPadding(px(c,11),px(c,11),px(c,11),px(c,11));
        v.setBackground(surface(Color.WHITE,0,c));
        v.setContentDescription(label);v.setClickable(true);v.setFocusable(true);return v;
    }
}
