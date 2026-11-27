package com.human9283.themovies;

import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Build;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class Diagnostics {
    private static final String TAG = "TheMovies";
    private static final long MAX_LOG_BYTES = 2L * 1024L * 1024L;
    private static Context appContext;
    private static File logDirectory;
    private static File appLog;
    private static File nativeLog;
    private static Thread.UncaughtExceptionHandler previousHandler;

    private Diagnostics() {}

    public static synchronized void init(Context context) {
        if (appContext != null) return;
        appContext = context.getApplicationContext();
        logDirectory = new File(appContext.getFilesDir(), "logs");
        if (!logDirectory.exists() && !logDirectory.mkdirs()) {
            Log.e(TAG, "Cannot create diagnostics directory: " + logDirectory);
            return;
        }
        appLog = new File(logDirectory, "app.log");
        nativeLog = new File(logDirectory, "native.log");
        rotateIfNeeded(appLog);
        rotateIfNeeded(nativeLog);
        log("Diagnostics initialized");
        log("Android " + Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")");
        previousHandler = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            log("UNCAUGHT EXCEPTION on thread " + thread.getName());
            appendStackTrace(error);
            Log.e(TAG, "Uncaught exception", error);
            if (previousHandler != null) previousHandler.uncaughtException(thread, error);
        });
    }

    public static synchronized void log(String message) {
        if (appLog == null) return;
        String timestamp = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(new Date());
        try (FileWriter writer = new FileWriter(appLog, true)) {
            writer.write(timestamp + " " + message + "\n");
        } catch (Exception error) {
            Log.e(TAG, "Cannot write diagnostics log", error);
        }
        Log.i(TAG, message);
    }

    private static synchronized void appendStackTrace(Throwable error) {
        if (appLog == null) return;
        try (PrintWriter writer = new PrintWriter(new FileWriter(appLog, true))) {
            error.printStackTrace(writer);
        } catch (Exception writeError) {
            Log.e(TAG, "Cannot save crash stack trace", writeError);
        }
    }

    private static void rotateIfNeeded(File file) {
        if (!file.isFile() || file.length() <= MAX_LOG_BYTES) return;
        File previous = new File(file.getParentFile(), file.getName() + ".1");
        if (previous.exists()) previous.delete();
        if (!file.renameTo(previous)) file.delete();
    }

    public static File createArchive(Context context) throws Exception {
        init(context);
        if (logDirectory == null) throw new IllegalStateException("Diagnostics directory is unavailable");
        File archive = new File(context.getCacheDir(), "the-movies-diagnostics.zip");
        if (archive.exists() && !archive.delete()) throw new IllegalStateException("Cannot replace previous diagnostics archive");
        captureLogcat();
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(archive))) {
            // Include every diagnostic emitted by the native host, DXVK and Android.
            addDirectory(zip, logDirectory, "logs");
            addText(zip, "device.txt", deviceInfo(context));
        }
        log("Diagnostics archive created: " + archive.length() + " bytes");
        return archive;
    }

    private static void captureLogcat() {
        File output = new File(logDirectory, "logcat.txt");
        try {
            // Restrict exported logcat to this application's process. A global
            // logcat dump can contain unrelated messages from other apps.
            String pid = Integer.toString(android.os.Process.myPid());
            Process process = Runtime.getRuntime().exec(new String[] {
                "logcat", "-d", "-v", "threadtime", "--pid=" + pid, "-t", "2000"
            });
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
                 FileWriter writer = new FileWriter(output, false)) {
                String line;
                while ((line = reader.readLine()) != null) {
                    writer.write(line);
                    writer.write("\n");
                }
            }
            process.waitFor();
        } catch (Exception error) {
            log("Could not capture logcat: " + error);
            try (FileWriter writer = new FileWriter(output, false)) {
                writer.write("Logcat capture unavailable: " + error + "\n");
            } catch (Exception ignored) {
                Log.e(TAG, "Cannot save logcat capture failure", ignored);
            }
        }
    }

    private static String deviceInfo(Context context) {
        String version = "unknown";
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            version = info.versionName + " (" + info.versionCode + ")";
        } catch (Exception ignored) {}
        return "App: " + context.getPackageName() + "\n"
            + "Version: " + version + "\n"
            + "Manufacturer: " + Build.MANUFACTURER + "\n"
            + "Model: " + Build.MODEL + "\n"
            + "Device: " + Build.DEVICE + "\n"
            + "Product: " + Build.PRODUCT + "\n"
            + "Android: " + Build.VERSION.RELEASE + "\n"
            + "SDK: " + Build.VERSION.SDK_INT + "\n"
            + "ABIs: " + join(Build.SUPPORTED_ABIS) + "\n"
            + "App files: " + context.getFilesDir().getAbsolutePath() + "\n";
    }

    private static String join(String[] values) {
        StringBuilder result = new StringBuilder();
        for (String value : values) {
            if (result.length() > 0) result.append(", ");
            result.append(value);
        }
        return result.toString();
    }

    private static void addFile(ZipOutputStream zip, File file, String name) throws Exception {
        if (file == null || !file.isFile()) return;
        zip.putNextEntry(new ZipEntry(name));
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) zip.write(buffer, 0, count);
        }
        zip.closeEntry();
    }

    private static void addDirectory(ZipOutputStream zip, File directory, String prefix) throws Exception {
        if (directory == null || !directory.isDirectory()) return;
        File[] children = directory.listFiles();
        if (children == null) return;
        for (File child : children) {
            String name = prefix + "/" + child.getName();
            if (child.isDirectory()) addDirectory(zip, child, name);
            else addFile(zip, child, name);
        }
    }

    private static void addText(ZipOutputStream zip, String name, String value) throws Exception {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(value.getBytes("UTF-8"));
        zip.closeEntry();
    }
}
