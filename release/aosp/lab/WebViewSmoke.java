package org.kobodroid.lab;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.webkit.WebView;
import android.webkit.WebViewClient;

public final class WebViewSmoke extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        WebView view = new WebView(this);
        view.getSettings().setJavaScriptEnabled(true);
        view.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView page, String url) {
                page.evaluateJavascript("document.getElementById('smoke').textContent", result -> {
                    if ("\"kobodroid-webview-ok\"".equals(result)) {
                        Log.i("KoboWebViewSmoke", "WEBVIEW_HTML_JS_PASS");
                    }
                });
            }
        });
        setContentView(view);
        view.loadData("<script>document.write('<p id=smoke>kobodroid-webview-ok</p>')</script>",
                "text/html", "UTF-8");
    }
}
