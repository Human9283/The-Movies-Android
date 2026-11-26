package com.human9283.d3d9probe;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
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

public final class VulkanPreflightActivity extends Activity {
    private TextView reportView;
    private Button copyButton;
    private Button launchButton;
    private String report = "Подготовка Vulkan preflight…\nОжидание Android Surface.";
    private boolean started;
    private boolean nativeLibraryReady;
    private String loadError = "";

    private native String runProbe(Surface surface);

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            System.loadLibrary("vulkanpreflight");
            nativeLibraryReady = true;
        } catch (Throwable error) {
            loadError = "Не удалось загрузить libvulkanpreflight.so: " + error;
            report = loadError;
        }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(14), dp(10), dp(14), dp(8));

        TextView title = new TextView(this);
        title.setText("The Movies — Vulkan / Surface preflight");
        title.setTextSize(19);
        title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        root.addView(title);

        TextView hint = new TextView(this);
        hint.setText("Сначала проверяется Android Surface, Vulkan-драйвер и present. DXVK не загружается до нажатия кнопки ниже.");
        hint.setTextSize(13);
        hint.setPadding(0, dp(5), 0, dp(7));
        root.addView(hint);

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setGravity(Gravity.CENTER_VERTICAL);

        copyButton = new Button(this);
        copyButton.setText("Скопировать отчёт");
        copyButton.setEnabled(false);
        copyButton.setOnClickListener(v -> copyReport());
        actions.addView(copyButton, new LinearLayout.LayoutParams(0, dp(48), 1f));

        launchButton = new Button(this);
        launchButton.setText("Запустить DXVK");
        launchButton.setEnabled(false);
        launchButton.setOnClickListener(v -> startActivity(new Intent(this, MainActivity.class)));
        actions.addView(launchButton, new LinearLayout.LayoutParams(0, dp(48), 1f));
        root.addView(actions);

        ScrollView scroll = new ScrollView(this);
        reportView = new TextView(this);
        reportView.setText(report);
        reportView.setTextSize(11);
        reportView.setTypeface(Typeface.MONOSPACE);
        reportView.setTextIsSelectable(true);
        reportView.setPadding(dp(8), dp(8), dp(8), dp(8));
        scroll.addView(reportView, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f);
        scrollParams.topMargin = dp(6);
        root.addView(scroll, scrollParams);

        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.setZOrderOnTop(false);
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                if (started) return;
                started = true;
                if (!nativeLibraryReady) {
                    report = loadError;
                    showCompletedReport();
                    return;
                }
                report = "Vulkan preflight выполняется…\n";
                reportView.setText(report);
                final Surface surface = holder.getSurface();
                new Thread(() -> {
                    String result;
                    try {
                        result = runProbe(surface);
                    } catch (Throwable error) {
                        result = "Ошибка JNI/Vulkan preflight: " + error;
                    }
                    final String finalResult = (result == null || result.isEmpty())
                            ? "Проверка завершилась без отчёта."
                            : result;
                    runOnUiThread(() -> {
                        report = finalResult;
                        showCompletedReport();
                    });
                }, "VulkanPreflight").start();
            }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}
            @Override public void surfaceDestroyed(SurfaceHolder holder) {}
        });
        root.addView(surfaceView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(105)));

        setContentView(root);
        if (!nativeLibraryReady) {
            reportView.setText(report);
            copyButton.setEnabled(true);
            launchButton.setEnabled(true);
        }
    }

    private void showCompletedReport() {
        reportView.setText(report);
        copyButton.setEnabled(true);
        launchButton.setEnabled(true);
    }

    private void copyReport() {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(ClipData.newPlainText("Vulkan preflight report", report));
        Toast.makeText(this, "Отчёт скопирован. Вставьте его в чат.", Toast.LENGTH_LONG).show();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
