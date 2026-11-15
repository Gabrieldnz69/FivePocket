package dev.recompbootstrap.smoke;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.StatFs;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * My Music (Update 1 P6): copies the player's own songs into the music folder (MyMusicLibrary.FOLDER in the app's external files
 * directory). The way in is Android's share sheet: songs shared with "Add to My Music" from a file manager (ACTION_SEND /
 * ACTION_SEND_MULTIPLE, audio/*). Started without songs it says how to add them (no file picker until the launcher has an Add Music
 * row: android-qol, NOTES Q1). Only content from local storage providers is read (MyMusicLibrary.localProvider): a cloud provider's
 * file is refused, so nothing comes over the network. Each file is copied to a hidden ".mm-*.part" file and renamed over its reserved
 * name (MyMusicLibrary.reserve / commit) when complete. Nothing in the folder is removed or replaced (a taken name gets " (2)").
 * Runs in the menu's process, never in the game's. Logs: tag RecompMyMusic.
 */
public final class MyMusicImportActivity extends Activity {
    private static final String TAG = "RecompMyMusic";
    private TextView text;
    private boolean finished;
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        text = new TextView(this);
        text.setTextColor(Color.WHITE);
        text.setBackgroundColor(Color.BLACK);
        text.setTextSize(20);
        text.setGravity(Gravity.CENTER);
        text.setPadding(48, 48, 48, 48);
        text.setOnClickListener(v -> { if (finished) finish(); });
        setContentView(text);
        if (state != null) { done("To see new songs in the game: side menu, My Music, Find New Music."); return; } // (recreated: no second copy)
        final List<Uri> shared = sharedUris(getIntent());
        if (!shared.isEmpty()) copy(shared);
        else done("No songs were shared. To add songs, share them from a file app and choose Add to My Music.");
    }

    private static List<Uri> sharedUris(Intent intent) {
        final List<Uri> out = new ArrayList<>();
        if (intent == null) return out;
        if (Intent.ACTION_SEND.equals(intent.getAction())) {
            final Uri u = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (u != null) out.add(u);
        } else if (Intent.ACTION_SEND_MULTIPLE.equals(intent.getAction())) {
            final ArrayList<Uri> list = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (list != null) for (Uri u : list) if (u != null) out.add(u);
        }
        return out;
    }

    private void copy(List<Uri> uris) {
        text.setText(uris.size() == 1 ? "Adding 1 song…" : "Adding " + uris.size() + " songs…");
        final File base = getExternalFilesDir(null);
        final File dir = base == null ? null : new File(base, MyMusicLibrary.FOLDER);
        final ContentResolver resolver = getContentResolver();
        new Thread(() -> {
            int added = 0;
            final List<String> refused = new ArrayList<>(), paths = new ArrayList<>();
            if (dir == null) { main.post(() -> done("The music folder is not available right now.")); return; }
            final boolean made = MyMusicLibrary.ensureFolder(dir);
            if (!dir.isDirectory()) { main.post(() -> done("The music folder could not be created.")); return; }
            if (made) paths.add(dir.getAbsolutePath());
            final Set<String> taken = new HashSet<>();
            final String[] names = dir.list();
            if (names != null) for (String n : names) taken.add(n);
            for (Uri u : uris) {
                String why;
                try { why = copyOne(resolver, u, dir, taken, paths); }
                catch (RuntimeException e) { why = "the copy failed."; Log.w(TAG, "MY_MUSIC import failed: " + e.getClass().getSimpleName()); }
                if (why == null) ++added; else refused.add(why);
            }
            // (so the new files show over USB at once)
            if (!paths.isEmpty()) MediaScannerConnection.scanFile(getApplicationContext(), paths.toArray(new String[0]), null, null);
            final int count = added;
            final String summary = (count == 0 ? "No song was added." : "Added " + count + (count == 1 ? " song" : " songs") + " to My Music.")
                    + (refused.isEmpty() ? "" : "\n" + refused.size() + " not added: " + refused.get(0))
                    + "\nIn the game: side menu, My Music, Find New Music.";
            Log.i(TAG, "MY_MUSIC import added=" + count + " refused=" + refused.size());
            main.post(() -> done(summary));
        }, "my-music-import").start();
    }

    /** Copies one file; null when added (its path joins paths), else the reason in words (lower case: it follows "not added: "). */
    private static String copyOne(ContentResolver resolver, Uri uri, File dir, Set<String> taken, List<String> paths) {
        if (!MyMusicLibrary.localProvider(uri.getScheme(), uri.getAuthority())) return MyMusicLibrary.NOT_LOCAL;
        String display = null;
        long size = -1;
        try (Cursor c = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                display = c.isNull(0) ? null : c.getString(0);
                size = c.isNull(1) ? -1 : c.getLong(1);
            }
        } catch (RuntimeException e) {
            return "the file could not be read.";
        }
        final String unsupported = "not a supported music file (MP3, M4A, AAC, FLAC, OGG, Opus, WAV).";
        if (MyMusicLibrary.safeName(display, taken) == null) return unsupported;
        final long free;
        try { free = new StatFs(dir.getPath()).getAvailableBytes(); }
        catch (IllegalArgumentException e) { return "the free space could not be read."; }
        final String refusal = MyMusicLibrary.refusal(size, free);
        if (refusal != null) return refusal;
        File dest = null, part = null;
        long copied = 0;
        try {
            dest = MyMusicLibrary.reserve(dir, display, taken);
            if (dest == null) return unsupported;
            part = File.createTempFile(".mm-", MyMusicLibrary.PART, dir); // (hidden and not an audio name: never scanned or played)
            try (InputStream in = resolver.openInputStream(uri); FileOutputStream out = new FileOutputStream(part)) {
                if (in == null) { MyMusicLibrary.abandon(part, dest); return "the file could not be read."; }
                final byte[] buffer = new byte[64 * 1024];
                for (int n; (n = in.read(buffer)) > 0; ) {
                    copied += n;
                    if (copied > MyMusicLibrary.MAX_FILE) throw new IOException("over the size limit");
                    out.write(buffer, 0, n);
                }
                out.getFD().sync();
            }
        } catch (IOException | RuntimeException e) {
            MyMusicLibrary.abandon(part, dest);
            return copied > MyMusicLibrary.MAX_FILE ? MyMusicLibrary.TOO_LARGE : "the copy failed.";
        }
        if (!MyMusicLibrary.commit(part, dest)) {
            MyMusicLibrary.abandon(part, dest);
            return "the copy failed.";
        }
        paths.add(dest.getAbsolutePath());
        return null;
    }

    private void done(String words) {
        finished = true;
        text.setText(words + "\n\nTap or press B to close.");
    }

    @Override public boolean dispatchKeyEvent(KeyEvent e) {
        if (finished && e.getAction() == KeyEvent.ACTION_UP && (e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_B
                || e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_A || e.getKeyCode() == KeyEvent.KEYCODE_BACK)) { finish(); return true; }
        return super.dispatchKeyEvent(e);
    }
}
