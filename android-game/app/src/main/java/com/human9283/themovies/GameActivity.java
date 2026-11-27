package com.human9283.themovies;

import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.util.Log;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    private static final String TAG = "TheMovies";


    @Override public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        Diagnostics.log("SDL orientation request " + width + "x" + height
            + " resizable=" + resizable + " hint=" + hint
            + "; forcing LANDSCAPE");
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    @Override protected void onCreate(android.os.Bundle state) {
        Diagnostics.init(this);
        Diagnostics.log("GameActivity onCreate");
        super.onCreate(state);
        logDisplayState("onCreate");
    }

    @Override protected void onResume() {
        super.onResume();
        logDisplayState("onResume");
    }

    @Override protected void onPause() {
        logDisplayState("onPause");
        super.onPause();
    }

    @Override public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        logDisplayState("onConfigurationChanged");
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        logDisplayState("onWindowFocusChanged focus=" + hasFocus);
    }

    private void logDisplayState(String event) {
        int width = getWindow().getDecorView().getWidth();
        int height = getWindow().getDecorView().getHeight();
        int orientation = getResources().getConfiguration().orientation;
        int rotation = getWindowManager().getDefaultDisplay().getRotation();
        Diagnostics.log(event + " display=" + width + "x" + height
            + " configOrientation=" + orientation + " rotation=" + rotation);
    }

    @Override protected String[] getLibraries() {
        Log.i(TAG, "Loading SDL2, Android DXVK Native and game host");
        Diagnostics.log("Loading SDL2, SDL2_ttf, DXVK D3D9 and native game host");
        return new String[] { "SDL2", "SDL2_ttf", "dxvk_d3d9", "main" };
    }
}
