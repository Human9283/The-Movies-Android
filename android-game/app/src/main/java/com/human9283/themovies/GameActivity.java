package com.human9283.themovies;

import android.util.Log;
import org.libsdl.app.SDLActivity;

public final class GameActivity extends SDLActivity {
    private static final String TAG = "TheMovies";
    @Override protected String[] getLibraries() {
        Log.i(TAG, "Loading SDL2, Android DXVK Native and game host");
        return new String[] { "SDL2", "dxvk_d3d9", "main" };
    }
}
