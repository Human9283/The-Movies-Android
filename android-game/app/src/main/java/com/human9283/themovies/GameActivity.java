package com.human9283.themovies;

import android.util.Log;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    private static final String TAG = "TheMovies";
    @Override protected void onCreate(android.os.Bundle state) {
        Diagnostics.init(this);
        Diagnostics.log("GameActivity created");
        super.onCreate(state);
    }
    @Override protected String[] getLibraries() {
        Log.i(TAG, "Loading SDL2, Android DXVK Native and game host");
        Diagnostics.log("Loading SDL2, SDL2_ttf, DXVK D3D9 and native game host");
        return new String[] { "SDL2", "SDL2_ttf", "dxvk_d3d9", "main" };
    }
}
