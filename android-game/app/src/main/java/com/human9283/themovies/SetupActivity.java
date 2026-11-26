package com.human9283.themovies;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public final class SetupActivity extends Activity {
    private static final int PICK_GAME_FOLDER = 4101;
    private File gameDir;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        gameDir = new File(getFilesDir(), "game");
        if (validInstall()) { launchGame(); return; }
        new AlertDialog.Builder(this)
            .setTitle("Импорт The Movies")
            .setMessage("Выберите папку установленной игры The Movies (внутри должны находиться Movies.exe и Data/). Файлы будут скопированы в закрытое хранилище приложения.")
            .setCancelable(false)
            .setPositiveButton("Выбрать папку", (d, w) -> pickFolder())
            .setNegativeButton("Выход", (d, w) -> finish())
            .show();
    }

    private boolean validInstall() {
        return new File(gameDir, "Movies.exe").isFile() && new File(gameDir, "Data").isDirectory();
    }

    private void pickFolder() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, PICK_GAME_FOLDER);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_GAME_FOLDER || result != RESULT_OK || data == null || data.getData() == null) {
            finish(); return;
        }
        Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            File staging = new File(getFilesDir(), "game-import");
            deleteRecursively(staging);
            if (!staging.mkdirs() && !staging.isDirectory()) throw new Exception("Не удалось создать каталог импорта");
            String rootId = DocumentsContract.getTreeDocumentId(tree);
            copyChildren(tree, rootId, staging);
            if (!new File(staging, "Movies.exe").isFile() || !new File(staging, "Data").isDirectory())
                throw new Exception("В выбранной папке не найдены Movies.exe и Data/");
            deleteRecursively(gameDir);
            if (!staging.renameTo(gameDir)) throw new Exception("Не удалось переместить файлы игры");
            Toast.makeText(this, "Игра импортирована", Toast.LENGTH_LONG).show();
            launchGame();
        } catch (Exception e) {
            deleteRecursively(new File(getFilesDir(), "game-import"));
            new AlertDialog.Builder(this).setTitle("Ошибка импорта").setMessage(e.getMessage())
                .setPositiveButton("Повторить", (d, w) -> pickFolder())
                .setNegativeButton("Выход", (d, w) -> finish()).show();
        }
    }

    private void copyChildren(Uri tree, String parentId, File destination) throws Exception {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] projection = { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE };
        try (Cursor c = getContentResolver().query(children, projection, null, null, null)) {
            if (c == null) throw new Exception("Проводник не вернул содержимое папки");
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (name == null || name.contains("/") || name.equals(".") || name.equals("..")) continue;
                File out = new File(destination, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!out.mkdirs() && !out.isDirectory()) throw new Exception("Не удалось создать " + name);
                    copyChildren(tree, id, out);
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
