package com.human9283.themoviesprobe;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public final class MainActivity extends Activity {
    static { System.loadLibrary("main"); }

    private TextView reportView;
    private Button copyButton;
    private String report = "Подготовка Vulkan-проверки...\\n\\nОжидание Android Surface.";
    private boolean started;

    private native String runProbe(Surface surface);

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(16), dp(16), dp(16), dp(12));

        TextView title = new TextView(this);
        title.setText("The Movies — Vulkan Probe");
        title.setTextSize(21);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        root.addView(title);

        TextView hint = new TextView(this);
        hint.setText("Диагностика графического драйвера телефона. Интернет и разрешения не требуются.");
        hint.setTextSize(14);
        hint.setPadding(0, dp(8), 0, dp(12));
        root.addView(hint);

        copyButton = new Button(this);
        copyButton.setText("Скопировать отчёт");
        copyButton.setEnabled(false);
        copyButton.setOnClickListener(v -> copyReport());
        root.addView(copyButton);

        ScrollView scroll = new ScrollView(this);
        reportView = new TextView(this);
        reportView.setText(report);
        reportView.setTextSize(12);
        reportView.setTypeface(Typeface.MONOSPACE);
        reportView.setTextIsSelectable(true);
        reportView.setPadding(dp(10), dp(10), dp(10), dp(10));
        scroll.addView(reportView, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        scrollParams.topMargin = dp(12);
        root.addView(scroll, scrollParams);

        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                if (started) return;
                started = true;
                report = "Проверка Vulkan выполняется...\\n";
                reportView.setText(report);
                new Thread(() -> {
                    String result;
                    try {
                        result = runProbe(holder.getSurface());
                    } catch (Throwable error) {
                        result = "Ошибка запуска диагностики: " + error;
                    }
                    final String finalResult = (result == null || result.isEmpty())
                            ? "Проверка завершилась без отчёта. Попробуйте перезапустить приложение."
                            : result;
                    runOnUiThread(() -> {
                        report = finalResult;
                        reportView.setText(report);
                        copyButton.setEnabled(true);
                    });
                }, "VulkanProbe").start();
            }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}
            @Override public void surfaceDestroyed(SurfaceHolder holder) {}
        });
        root.addView(surfaceView, new LinearLayout.LayoutParams(1, 1));

        setContentView(root);
    }

    private void copyReport() {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(ClipData.newPlainText("Vulkan probe report", report));
        Toast.makeText(this, "Отчёт скопирован. Вернитесь в ChatGPT и вставьте его в чат.", Toast.LENGTH_LONG).show();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
