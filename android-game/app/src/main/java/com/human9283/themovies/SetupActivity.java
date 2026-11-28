package com.human9283.themovies;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.HashSet;
import java.util.Set;

public final class SetupActivity extends Activity {
    private static final int PICK_GAME_FOLDER = 4101;
    private static final int CREATE_DIAGNOSTICS = 4102;
    private File gameDir;
    private File pendingDiagnostics;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        Diagnostics.init(this);
        gameDir = new File(getFilesDir(), "game");
        if (validInstall()) {
            showInstalledMenu();
        } else if (BundledGameInstaller.hasBundledGame(this)) {
            installBundledGame();
        } else {
            showImportPrompt();
        }
    }

    private void installBundledGame() {
        ProgressDialog progress = new ProgressDialog(this);
        progress.setTitle("Установка The Movies");
        progress.setMessage("Подготовка игровых данных...");
        progress.setProgressStyle(ProgressDialog.STYLE_HORIZONTAL);
        progress.setMax(1000);
        progress.setProgress(0);
        progress.setCancelable(false);
        progress.show();

        new Thread(() -> {
            try {
                BundledGameInstaller.install(this, gameDir, (done, total, filesDone, totalFiles) ->
                    runOnUiThread(() -> {
                        int percent = total > 0 ? (int) Math.min(1000, done * 1000L / total) : 0;
                        progress.setProgress(percent);
                        progress.setMessage(String.format(Locale.US,
                            "Распаковка игровых данных: %d%% (%d/%d файлов)",
                            percent / 10, filesDone, totalFiles));
                    }));
                runOnUiThread(() -> {
                    progress.dismiss();
                    Diagnostics.log("Bundled game data installed and SHA-256 verified");
                    launchGame();
                });
            } catch (Exception error) {
                Diagnostics.log("Bundled game installation failed: " + error);
                runOnUiThread(() -> {
                    progress.dismiss();
                    new AlertDialog.Builder(this)
                        .setTitle("Не удалось установить игру")
                        .setMessage(error.getMessage() == null ? error.toString() : error.getMessage())
                        .setPositiveButton("Повторить", (d, w) -> installBundledGame())
                        .setNeutralButton("Импортировать вручную", (d, w) -> pickFolder())
                        .setNegativeButton("Выход", (d, w) -> finish())
                        .setCancelable(false)
                        .show();
                });
            }
        }, "bundled-game-installer").start();
    }

    private boolean validInstall() {
        return new File(gameDir, "Movies.exe").isFile() && new File(gameDir, "Data").isDirectory();
    }

    private void showInstalledMenu() {
        new AlertDialog.Builder(this)
            .setTitle("The Movies")
            .setMessage("Файлы игры найдены. Можно запустить игру или экспортировать диагностические данные.")
            .setCancelable(false)
            .setPositiveButton("Запустить игру", (d, w) -> launchGame())
            .setNeutralButton("Экспорт диагностики", (d, w) -> exportDiagnostics())
            .setNegativeButton("Выход", (d, w) -> finish())
            .show();
    }

    private void showImportPrompt() {
        new AlertDialog.Builder(this)
            .setTitle("Импорт The Movies")
            .setMessage("Выберите папку установленной игры The Movies (внутри должны находиться Movies.exe и Data/). Файлы будут скопированы в закрытое хранилище приложения.")
            .setCancelable(false)
            .setPositiveButton("Выбрать папку", (d, w) -> pickFolder())
            .setNeutralButton("Экспорт диагностики", (d, w) -> exportDiagnostics())
            .setNegativeButton("Выход", (d, w) -> finish())
            .show();
    }

    private void exportDiagnostics() {
        try {
            pendingDiagnostics = Diagnostics.createArchive(this);
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/zip");
            intent.putExtra(Intent.EXTRA_TITLE, "the-movies-diagnostics.zip");
            startActivityForResult(intent, CREATE_DIAGNOSTICS);
        } catch (Exception error) {
            Diagnostics.log("Diagnostics export failed: " + error);
            new AlertDialog.Builder(this).setTitle("Ошибка экспорта")
                .setMessage(error.toString())
                .setPositiveButton("OK", null).show();
        }
    }

    private void pickFolder() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, PICK_GAME_FOLDER);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == CREATE_DIAGNOSTICS) {
            if (result == RESULT_OK && data != null && data.getData() != null && pendingDiagnostics != null) {
                try (InputStream input = new java.io.FileInputStream(pendingDiagnostics);
                     OutputStream output = getContentResolver().openOutputStream(data.getData())) {
                    if (output == null) throw new Exception("Could not open destination");
                    byte[] buffer = new byte[8192];
                    int count;
                    while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                    Toast.makeText(this, "Диагностика сохранена", Toast.LENGTH_LONG).show();
                    Diagnostics.log("Diagnostics archive exported");
                } catch (Exception error) {
                    Diagnostics.log("Diagnostics archive copy failed: " + error);
                    Toast.makeText(this, "Не удалось сохранить архив: " + error, Toast.LENGTH_LONG).show();
                }
            }
            if (pendingDiagnostics != null) pendingDiagnostics.delete();
            pendingDiagnostics = null;
            return;
        }
        if (request != PICK_GAME_FOLDER || result != RESULT_OK || data == null || data.getData() == null) {
            if (request == PICK_GAME_FOLDER) finish();
            return;
        }
        Uri tree = data.getData();
        ProgressDialog progress = new ProgressDialog(this);
        progress.setTitle("Импорт The Movies");
        progress.setMessage("Копирование игровых данных...");
        progress.setProgressStyle(ProgressDialog.STYLE_SPINNER);
        progress.setCancelable(false);
        progress.show();
        new Thread(() -> {
            try {
                importGameFolder(tree);
                runOnUiThread(() -> {
                    progress.dismiss();
                    Toast.makeText(this, "Игра импортирована", Toast.LENGTH_LONG).show();
                    Diagnostics.log("Game data import completed");
                    launchGame();
                });
            } catch (Exception e) {
                deleteRecursively(new File(getFilesDir(), "game-import"));
                Diagnostics.log("Game data import failed: " + e);
                runOnUiThread(() -> {
                    progress.dismiss();
                    new AlertDialog.Builder(this).setTitle("Ошибка импорта")
                        .setMessage(e.getMessage() == null ? e.toString() : e.getMessage())
                        .setPositiveButton("Повторить", (d, w) -> pickFolder())
                        .setNegativeButton("Экспорт диагностики", (d, w) -> exportDiagnostics()).show();
                });
            }
        }, "game-folder-import").start();
    }

    private void importGameFolder(Uri tree) throws Exception {
        File staging = new File(getFilesDir(), "game-import");
        deleteRecursively(staging);
        if (!staging.mkdirs() && !staging.isDirectory()) throw new Exception("Не удалось создать каталог импорта");
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        if (rootId == null || rootId.isEmpty()) throw new Exception("Проводник не вернул идентификатор папки");
        copyChildren(tree, rootId, staging, new HashSet<>(), 0);
        if (!new File(staging, "Movies.exe").isFile() || !new File(staging, "Data").isDirectory())
            throw new Exception("В выбранной папке не найдены Movies.exe и Data/");
        File backup = new File(getFilesDir(), "game-previous");
        deleteRecursively(backup);
        boolean hadInstall = gameDir.exists();
        if (hadInstall && !gameDir.renameTo(backup))
            throw new Exception("Не удалось сохранить предыдущую установку");
        if (!staging.renameTo(gameDir)) {
            boolean restored = !hadInstall || backup.renameTo(gameDir);
            if (!restored) {
                throw new Exception("Не удалось активировать импорт и восстановить прежнюю установку. "
                    + "Предыдущая установка сохранена в " + backup.getAbsolutePath());
            }
            throw new Exception("Не удалось активировать импортированную установку; прежняя установка восстановлена");
        }
        deleteRecursively(backup);
    }

    private void copyChildren(Uri tree, String parentId, File destination,
                              Set<String> visited, int depth) throws Exception {
        if (depth > 128 || parentId == null || parentId.isEmpty() || !visited.add(parentId))
            throw new Exception("Проводник вернул циклическую или слишком глубокую структуру папок");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] projection = { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE };
        try (Cursor c = getContentResolver().query(children, projection, null, null, null)) {
            if (c == null) throw new Exception("Проводник не вернул содержимое папки");
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (id == null || id.isEmpty() || name == null || name.isEmpty()
                        || name.equals(".") || name.equals("..") || name.contains("/")
                        || name.contains("\\") || name.indexOf('\0') >= 0) {
                    throw new Exception("Проводник вернул недопустимое имя или идентификатор файла");
                }
                File out = new File(destination, name);
                if (out.exists()) throw new Exception("В папке есть повторяющееся имя: " + name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!out.mkdir()) throw new Exception("Не удалось создать " + name);
                    copyChildren(tree, id, out, visited, depth + 1);
                } else {
                    Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    try (InputStream in = getContentResolver().openInputStream(document);
                         FileOutputStream stream = new FileOutputStream(out)) {
                        if (in == null) throw new Exception("Не удалось открыть " + name);
                        byte[] buffer = new byte[1024 * 1024];
                        int n;
                        while ((n = in.read(buffer)) >= 0) if (n > 0) stream.write(buffer, 0, n);
                    }
                }
            }
        }
    }

    private void launchGame() {
        Diagnostics.log("Launching game activity");
        startActivity(new Intent(this, GameActivity.class));
        finish();
    }

    private static void deleteRecursively(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteRecursively(child);
        }
        if (file.exists()) file.delete();
    }
}
