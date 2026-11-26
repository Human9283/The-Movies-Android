package com.human9283.d3d9probe;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.graphics.Color;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.util.Log;

import org.libsdl.app.SDLActivity;

public final class MainActivity extends SDLActivity {
    private static final String TAG = "TM-D3D9-ACTIVITY";
    private static final String PREFS = "dxvk_crash_trace";
    private static final String TRACE = "trace";

    private void saveTrace(String line) {
        android.content.SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        String old = prefs.getString(TRACE, "");
        String next = (old + line + "\\n");
        if (next.length() > 24000) next = next.substring(next.length() - 24000);
        prefs.edit().putString(TRACE, next).commit();
    }
    private TextView statusView;

    private native String nativeGetProbeReport();

    @Override
    protected String[] getLibraries() {
        Log.i(TAG, "SDLActivity requested native libraries");
        saveTrace("JAVA: SDLActivity requested native libraries");
        // Load only SDL and the probe entry point here. DXVK is loaded lazily
        // from native code with dlopen(), so a linker/dependency error can be
        // reported in-app instead of aborting SDLActivity startup.
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "SDLActivity onCreate enter");
        saveTrace("JAVA: SDLActivity onCreate enter");
        super.onCreate(savedInstanceState);
        Log.i(TAG, "SDLActivity onCreate returned; brokenLibraries=" + mBrokenLibraries + ", layout=" + (mLayout != null));
        saveTrace("JAVA: SDLActivity onCreate returned; brokenLibraries=" + mBrokenLibraries + ", layout=" + (mLayout != null));
        if (mBrokenLibraries || mLayout == null) return;

        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.HORIZONTAL);
        panel.setGravity(android.view.Gravity.CENTER_VERTICAL);
        panel.setPadding(dp(8), dp(4), dp(8), dp(4));
        panel.setBackgroundColor(0xD0101820);

        statusView = new TextView(this);
        statusView.setText("Starting SDL2 / DXVK D3D9 test…");
        statusView.setTextColor(Color.WHITE);
        statusView.setTextSize(12);
        statusView.setMaxLines(2);
        panel.addView(statusView, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

        Button copy = new Button(this);
        copy.setText("Copy report");
        copy.setAllCaps(false);
        copy.setOnClickListener(v -> {
            String report;
            try {
                report = nativeGetProbeReport();
            } catch (Throwable error) {
                report = "Could not read native report: " + error;
            }
            ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (clipboard != null) {
                clipboard.setPrimaryClip(ClipData.newPlainText("DXVK D3D9 probe", report));
                Toast.makeText(this, "Report copied", Toast.LENGTH_SHORT).show();
            }
        });
        panel.addView(copy, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(44)));

        RelativeLayout.LayoutParams layout = new RelativeLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        layout.addRule(RelativeLayout.ALIGN_PARENT_TOP);
        mLayout.addView(panel, layout);
    }

    public void updateProbeStatus(String status) {
        Log.i(TAG, status);
        saveTrace("NATIVE: " + status);
        runOnUiThread(() -> {
            if (statusView != null) statusView.setText(status);
        });
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
