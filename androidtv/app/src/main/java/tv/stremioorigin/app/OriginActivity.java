package tv.stremioorigin.app;

import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.RelativeLayout;

import androidx.media3.ui.SubtitleView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Stremio Origin on Android TV: SDL runs the C app (libmain.so) on a GL surface drawn with transparency, and the
 * video plays on a SurfaceView under it (OriginPlayer), as on webOS where the picture is on the TV's video plane
 * under the app's window.
 */
public class OriginActivity extends SDLActivity {
    private static final String TAG = "StremioOrigin";
    static OriginActivity current;
    SurfaceView videoView;
    SubtitleView subtitles;

    @Override
    protected String[] getLibraries() { return new String[] { "SDL2", "main" }; }

    @Override
    protected void onCreate(Bundle state) {
        current = this;
        try { copyAssets(); } catch (IOException e) { Log.e(TAG, "assets", e); }
        // where the torrent engine (libsbengine.so) is: src/core/p2p.c reads it on Android
        try { android.system.Os.setenv("SB_LIBDIR", getApplicationInfo().nativeLibraryDir, true); } catch (Exception e) { Log.e(TAG, "SB_LIBDIR", e); }
        fromFreeTV(getIntent());
        // development builds only (the CI's emulator test): SB_P2PTEST, the app's own torrent check (src/main.c), from the intent
        if (BuildConfig.DEBUG && getIntent() != null) {
            String t = getIntent().getStringExtra("sb.p2ptest");
            if (t != null && t.matches("p2p://[0-9a-f]{40}(/\\d{1,4})?")) {
                try { android.system.Os.setenv("SB_P2PTEST", t, true); } catch (Exception e) { Log.e(TAG, "SB_P2PTEST", e); }
            }
        }
        super.onCreate(state);
        if (mBrokenLibraries || mLayout == null) return;
        // The picture: under everything. SDL's surface goes over it (a media overlay) with an alpha channel, so where the
        // app draws nothing the video shows through.
        videoView = new SurfaceView(this);
        mLayout.addView(videoView, 0, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        subtitles = new SubtitleView(this);
        subtitles.setUserDefaultStyle();
        subtitles.setUserDefaultTextSize();
        mLayout.addView(subtitles, 1, new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if (mSurface != null) {
            mSurface.setZOrderMediaOverlay(true);
            mSurface.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        }
        OriginPlayer.attach(this);
    }

    @Override
    protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        fromFreeTV(intent);
    }

    /**
     * FreeTV on the same TV asks for one title (its picks, Continue watching) and may give its TMDB key, as its store service does on
     * webOS: the same files in the app's own folder, open.json {"type","id","at"} (src/core/app.c looks for it once a second) and
     * tmdb.key (only when there is none). Only movie/series and an IMDb id; only a key of plain characters.
     */
    private void fromFreeTV(android.content.Intent i) {
        if (i == null) return;
        String type = i.getStringExtra("freetv.open.type"), id = i.getStringExtra("freetv.open.id"), key = i.getStringExtra("freetv.tmdb.key");
        try {
            if (type != null && id != null && type.matches("movie|series") && id.matches("tt\\d{5,10}")) {
                String body = "{\"type\":\"" + type + "\",\"id\":\"" + id + "\",\"at\":" + (System.currentTimeMillis() / 1000) + "}";
                write(new File(getFilesDir(), "open.json"), body);
            }
            File kf = new File(getFilesDir(), "tmdb.key");
            if (key != null && key.matches("[A-Za-z0-9._-]{16,400}") && (!kf.isFile() || kf.length() == 0)) write(kf, key);
        } catch (IOException e) { Log.e(TAG, "from FreeTV", e); }
    }

    private static void write(File f, String text) throws IOException {
        File tmp = new File(f.getPath() + ".tmp");
        try (OutputStream o = new FileOutputStream(tmp)) { o.write(text.getBytes(StandardCharsets.UTF_8)); }
        if (!tmp.renameTo(f)) throw new IOException("rename " + f);
    }

    @Override
    protected void onDestroy() {
        OriginPlayer.release();
        if (current == this) current = null;
        super.onDestroy();
    }

    /**
     * The remote's keys SDL leaves unknown become keys it knows, which src/platform/android/plat_android.c maps to the
     * app's: the colour keys to F1-F4, channel up/down to Page Up/Down, Play and Pause alone to Play/Pause and Break,
     * skip to fast forward / rewind.
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        int to;
        switch (e.getKeyCode()) {
            case KeyEvent.KEYCODE_PROG_RED: to = KeyEvent.KEYCODE_F1; break;
            case KeyEvent.KEYCODE_PROG_GREEN: to = KeyEvent.KEYCODE_F2; break;
            case KeyEvent.KEYCODE_PROG_YELLOW: to = KeyEvent.KEYCODE_F3; break;
            case KeyEvent.KEYCODE_PROG_BLUE: to = KeyEvent.KEYCODE_F4; break;
            case KeyEvent.KEYCODE_CHANNEL_UP: to = KeyEvent.KEYCODE_PAGE_UP; break;
            case KeyEvent.KEYCODE_CHANNEL_DOWN: to = KeyEvent.KEYCODE_PAGE_DOWN; break;
            case KeyEvent.KEYCODE_MEDIA_PLAY: to = KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE; break;
            case KeyEvent.KEYCODE_MEDIA_PAUSE: to = KeyEvent.KEYCODE_BREAK; break;
            case KeyEvent.KEYCODE_MEDIA_SKIP_FORWARD: case KeyEvent.KEYCODE_MEDIA_STEP_FORWARD: to = KeyEvent.KEYCODE_MEDIA_FAST_FORWARD; break;
            case KeyEvent.KEYCODE_MEDIA_SKIP_BACKWARD: case KeyEvent.KEYCODE_MEDIA_STEP_BACKWARD: to = KeyEvent.KEYCODE_MEDIA_REWIND; break;
            default: to = 0;
        }
        if (to != 0) {
            e = new KeyEvent(e.getDownTime(), e.getEventTime(), e.getAction(), to, e.getRepeatCount(), e.getMetaState(),
                    e.getDeviceId(), e.getScanCode(), e.getFlags(), e.getSource());
        }
        return super.dispatchKeyEvent(e);
    }

    /** The picture's rectangle, given in the app's 1920x1080 layout units. */
    void placeVideo(final int x, final int y, final int w, final int h) {
        runOnUiThread(new Runnable() { public void run() {
            if (videoView == null || mLayout == null) return;
            int lw = mLayout.getWidth(), lh = mLayout.getHeight();
            if (lw <= 0 || lh <= 0) { lw = 1920; lh = 1080; }
            RelativeLayout.LayoutParams p = new RelativeLayout.LayoutParams(Math.max(1, w * lw / 1920), Math.max(1, h * lh / 1080));
            p.leftMargin = x * lw / 1920;
            p.topMargin = y * lh / 1080;
            videoView.setLayoutParams(p);
            if (subtitles != null) subtitles.setLayoutParams(new RelativeLayout.LayoutParams(p));
        }});
    }

    void keepScreenOn(final boolean on) {
        runOnUiThread(new Runnable() { public void run() {
            if (on) getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            else getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }});
    }

    /**
     * The app reads its fonts, translations, sounds and pictures from files (as on the TV): the APK's assets/origin/ is
     * copied to files/assets once per installed version (a stamp holds the version it came from).
     */
    private void copyAssets() throws IOException {
        File dest = new File(getFilesDir(), "assets");
        File stamp = new File(getFilesDir(), "assets.stamp");
        String want;
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            want = pi.versionName + "/" + pi.lastUpdateTime;
        } catch (Exception e) { want = "?"; }
        if (dest.isDirectory() && stamp.isFile() && want.equals(read(stamp))) return;
        deleteTree(dest);
        copyTree(getAssets(), "origin", dest);
        try (OutputStream o = new FileOutputStream(stamp)) { o.write(want.getBytes(StandardCharsets.UTF_8)); }
        Log.i(TAG, "assets copied for " + want);
    }

    private static void copyTree(AssetManager am, String from, File to) throws IOException {
        String[] names = am.list(from);
        if (names != null && names.length > 0) {
            if (!to.isDirectory() && !to.mkdirs()) throw new IOException("mkdir " + to);
            for (String n : names) copyTree(am, from + "/" + n, new File(to, n));
            return;
        }
        try (InputStream in = am.open(from); OutputStream out = new FileOutputStream(to)) {
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        //noinspection ResultOfMethodCallIgnored
        f.delete();
    }

    private static String read(File f) {
        try (InputStream in = new FileInputStream(f)) {
            byte[] b = new byte[(int) Math.min(f.length(), 4096)];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n, StandardCharsets.UTF_8) : "";
        } catch (IOException e) { return ""; }
    }

    static View root() { return mLayout; }
}
