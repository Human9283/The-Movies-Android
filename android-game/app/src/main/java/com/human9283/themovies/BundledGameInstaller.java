package com.human9283.themovies;

import android.content.Context;
import android.content.res.AssetManager;
import android.os.StatFs;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

public final class BundledGameInstaller {
    private static final String ARCHIVE_ASSET = "game-data.tar";
    private static final String MANIFEST_ASSET = "game-manifest.json";
    private static final long EXTRA_SPACE_BYTES = 128L * 1024L * 1024L;
    private static final int TAR_BLOCK = 512;

    public interface ProgressListener {
        void onProgress(long bytesDone, long totalBytes, int filesDone, int totalFiles);
    }

    private static final class ExpectedFile {
        final long size;
        final String sha256;
        ExpectedFile(long size, String sha256) {
            this.size = size;
            this.sha256 = sha256;
        }
    }

    private BundledGameInstaller() {}

    public static boolean hasBundledGame(Context context) {
        try (InputStream archive = context.getAssets().open(ARCHIVE_ASSET);
             InputStream manifest = context.getAssets().open(MANIFEST_ASSET)) {
            return archive != null && manifest != null;
        } catch (IOException ignored) {
            return false;
        }
    }

    public static void install(Context context, File target, ProgressListener listener) throws Exception {
        JSONObject manifest;
        try (InputStream input = context.getAssets().open(MANIFEST_ASSET)) {
            byte[] data = readAll(input);
            manifest = new JSONObject(new String(data, java.nio.charset.StandardCharsets.UTF_8));
        }

        JSONArray entries = manifest.getJSONArray("files");
        long expectedTotal = manifest.getLong("total_bytes");
        if (entries.length() == 0 || expectedTotal <= 0) {
            throw new IOException("The bundled game manifest is empty or invalid");
        }

        Map<String, ExpectedFile> expected = new HashMap<>();
        Set<String> windowsPaths = new HashSet<>();
        long manifestBytes = 0;
        for (int i = 0; i < entries.length(); i++) {
            JSONObject item = entries.getJSONObject(i);
            String path = item.getString("path");
            long size = item.getLong("size");
            String hash = item.getString("sha256").toLowerCase(Locale.ROOT);
            if (!isSafeRelativePath(path) || size < 0 || !hash.matches("[0-9a-f]{64}")
                    || !windowsPaths.add(path.toLowerCase(Locale.ROOT))
                    || expected.put(path, new ExpectedFile(size, hash)) != null) {
                throw new IOException("Invalid or duplicate game manifest entry: " + path);
            }
            try {
                manifestBytes = Math.addExact(manifestBytes, size);
            } catch (ArithmeticException overflow) {
                throw new IOException("Game manifest byte count overflows", overflow);
            }
        }
        if (manifestBytes != expectedTotal) {
            throw new IOException("Game manifest total_bytes does not match the sum of its file sizes");
        }
        final long requiredSpace;
        try {
            requiredSpace = Math.addExact(expectedTotal, EXTRA_SPACE_BYTES);
        } catch (ArithmeticException overflow) {
            throw new IOException("Game manifest size is too large", overflow);
        }

        StatFs storage = new StatFs(context.getFilesDir().getAbsolutePath());
        long available = storage.getAvailableBytes();
        if (available < requiredSpace) {
            throw new IOException(String.format(Locale.US,
                "Not enough free internal storage. Need at least %.2f GB free; available %.2f GB.",
                requiredSpace / 1_000_000_000.0,
                available / 1_000_000_000.0));
        }

        File staging = new File(context.getFilesDir(), "game-bundle-import");
        deleteRecursively(staging);
        if (!staging.mkdirs() && !staging.isDirectory()) {
            throw new IOException("Could not create the game installation directory");
        }

        try (InputStream archive = context.getAssets().open(ARCHIVE_ASSET, AssetManager.ACCESS_STREAMING)) {
            extractTar(archive, staging, expected, expectedTotal, listener);
        } catch (Exception error) {
            deleteRecursively(staging);
            throw error;
        }

        if (!new File(staging, "Movies.exe").isFile() || !new File(staging, "Data").isDirectory()) {
            deleteRecursively(staging);
            throw new IOException("The bundled archive is missing Movies.exe or Data/");
        }

        File backup = new File(context.getFilesDir(), "game-previous");
        deleteRecursively(backup);
        boolean hadTarget = target.exists();
        if (hadTarget && !target.renameTo(backup)) {
            deleteRecursively(staging);
            throw new IOException("Could not preserve the previous game installation");
        }
        if (!staging.renameTo(target)) {
            boolean restored = !hadTarget || backup.renameTo(target);
            deleteRecursively(staging);
            if (!restored) {
                throw new IOException("Could not activate the bundled installation or restore the previous one. "
                    + "The previous installation is preserved at " + backup.getAbsolutePath());
            }
            throw new IOException("Could not activate the bundled game installation; the previous installation was restored");
        }
        deleteRecursively(backup);
    }

    private static void extractTar(InputStream input, File root, Map<String, ExpectedFile> expected,
                                   long totalBytes, ProgressListener listener) throws Exception {
        Set<String> seen = new HashSet<>();
        long bytesDone = 0;
        int filesDone = 0;
        byte[] header = new byte[TAR_BLOCK];
        byte[] buffer = new byte[1024 * 1024];

        while (true) {
            readFully(input, header, 0, TAR_BLOCK);
            if (isZeroBlock(header)) break;
            verifyTarChecksum(header);

            String name = readString(header, 0, 100);
            String prefix = readString(header, 345, 155);
            if (!prefix.isEmpty()) name = prefix + "/" + name;
            name = name.replaceFirst("/+$", "");
            if (name.isEmpty() || name.equals(".")) {
                skipFully(input, parseOctal(header, 124, 12));
                continue;
            }
            if (!isSafeRelativePath(name)) throw new IOException("Unsafe path in game archive: " + name);

            long size = parseOctal(header, 124, 12);
            if (size < 0) throw new IOException("Invalid file size in game archive: " + name);
            char type = (char) header[156];

            File output = new File(root, name);
            String rootPath = root.getCanonicalPath() + File.separator;
            if (!output.getCanonicalPath().startsWith(rootPath)) {
                throw new IOException("Game archive path escapes the installation directory: " + name);
            }

            if (type == '5') {
                if (!output.mkdirs() && !output.isDirectory()) throw new IOException("Could not create directory: " + name);
                skipFully(input, size);
            } else if (type == 0 || type == '0') {
                ExpectedFile item = expected.get(name);
                if (item == null || item.size != size || !seen.add(name)) {
                    throw new IOException("Unexpected, duplicate or incorrectly sized game file: " + name);
                }
                File parent = output.getParentFile();
                if (parent != null && !parent.mkdirs() && !parent.isDirectory()) {
                    throw new IOException("Could not create directory for: " + name);
                }
                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                try (FileOutputStream out = new FileOutputStream(output)) {
                    long remaining = size;
                    while (remaining > 0) {
                        int count = input.read(buffer, 0, (int) Math.min(buffer.length, remaining));
                        if (count < 0) throw new IOException("Unexpected end of game archive while reading: " + name);
                        if (count == 0) continue;
                        out.write(buffer, 0, count);
                        digest.update(buffer, 0, count);
                        remaining -= count;
                        bytesDone += count;
                        if (listener != null) listener.onProgress(bytesDone, totalBytes, filesDone, expected.size());
                    }
                }
                String actualHash = toHex(digest.digest());
                if (!actualHash.equals(item.sha256)) {
                    output.delete();
                    throw new IOException("SHA-256 verification failed for: " + name);
                }
                filesDone++;
                if (listener != null) listener.onProgress(bytesDone, totalBytes, filesDone, expected.size());
            } else {
                throw new IOException("Unsupported TAR entry type in game archive: " + name);
            }

            long padding = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
            skipFully(input, padding);
        }

        if (seen.size() != expected.size() || bytesDone != totalBytes) {
            throw new IOException("The bundled game archive is incomplete: extracted " + seen.size()
                + " of " + expected.size() + " files and " + bytesDone + " of " + totalBytes + " bytes");
        }
    }

    private static boolean isSafeRelativePath(String path) {
        if (path == null || path.isEmpty() || path.startsWith("/") || path.indexOf('\\') >= 0) return false;
        String[] parts = path.split("/");
        for (String part : parts) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;
        }
        return true;
    }

    private static void verifyTarChecksum(byte[] header) throws IOException {
        long stored = parseOctal(header, 148, 8);
        long sum = 0;
        for (int i = 0; i < header.length; i++) sum += (i >= 148 && i < 156) ? 32 : (header[i] & 0xff);
        if (stored != sum) throw new IOException("Invalid TAR header checksum");
    }

    private static long parseOctal(byte[] data, int offset, int length) throws IOException {
        long value = 0;
        boolean found = false;
        for (int i = offset; i < offset + length; i++) {
            int c = data[i] & 0xff;
            if (c == 0 || c == ' ') {
                if (found) break;
                continue;
            }
            if (c < '0' || c > '7') throw new IOException("Invalid TAR numeric field");
            found = true;
            int digit = c - '0';
            if (value > (Long.MAX_VALUE - digit) / 8) throw new IOException("TAR numeric field overflows");
            value = (value << 3) + digit;
        }
        return value;
    }

    private static String readString(byte[] data, int offset, int length) {
        int end = offset;
        while (end < offset + length && data[end] != 0) end++;
        return new String(data, offset, end - offset, java.nio.charset.StandardCharsets.UTF_8);
    }

    private static boolean isZeroBlock(byte[] block) {
        for (byte value : block) if (value != 0) return false;
        return true;
    }

    private static void readFully(InputStream input, byte[] data, int offset, int length) throws IOException {
        int done = 0;
        while (done < length) {
            int count = input.read(data, offset + done, length - done);
            if (count < 0) throw new IOException("Unexpected end of game archive");
            if (count > 0) done += count;
        }
    }

    private static void skipFully(InputStream input, long length) throws IOException {
        byte[] discard = new byte[8192];
        long remaining = length;
        while (remaining > 0) {
            long skipped = input.skip(remaining);
            if (skipped > 0) {
                remaining -= skipped;
            } else {
                int count = input.read(discard, 0, (int) Math.min(discard.length, remaining));
                if (count < 0) throw new IOException("Unexpected end of game archive");
                remaining -= count;
            }
        }
    }

    private static byte[] readAll(InputStream input) throws IOException {
        java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int count;
        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
        return output.toByteArray();
    }

    private static String toHex(byte[] bytes) {
        StringBuilder out = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) out.append(String.format(Locale.ROOT, "%02x", value & 0xff));
        return out.toString();
    }

    private static void deleteRecursively(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteRecursively(child);
        }
        if (file.exists()) file.delete();
    }
}
