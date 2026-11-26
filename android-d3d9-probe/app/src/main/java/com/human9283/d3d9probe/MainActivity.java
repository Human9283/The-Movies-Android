package com.human9283.d3d9probe;

import org.libsdl.app.SDLActivity;

public final class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // SDL must be loaded before DXVK; the app entry point is loaded last.
        return new String[] { "SDL2", "dxvk_d3d9", "main" };
    }
}
