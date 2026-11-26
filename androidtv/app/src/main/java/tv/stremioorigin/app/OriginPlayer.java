package tv.stremioorigin.app;

import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.util.Log;
import android.view.SurfaceHolder;

import androidx.annotation.NonNull;
import androidx.media3.common.C;
import androidx.media3.common.ColorInfo;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.TrackGroup;
import androidx.media3.common.TrackSelectionOverride;
import androidx.media3.common.Tracks;
import androidx.media3.common.VideoSize;
import androidx.media3.common.text.CueGroup;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.datasource.DefaultDataSource;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;
import androidx.media3.exoplayer.trackselection.DefaultTrackSelector;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * The player behind src/platform/android/video_android.c. Every call from C only posts work to the player's own thread;
 * the C side reads one snapshot (poll) per frame. States and fields follow src/core/video.h.
 */
@UnstableApi
public final class OriginPlayer {
    private static final String TAG = "StremioOrigin";
    // video.h's VidState
    private static final int IDLE = 0, LOADING = 1, PLAYING = 2, PAUSED = 3, BUFFERING = 4, ENDED = 5, ERROR = 6;

    private static HandlerThread thread;
    private static Handler h;
    private static ExoPlayer player;
    private static OriginActivity activity;

    // the snapshot, written on the player thread
    private static volatile double[] snap = new double[13];
    private static volatile String error = "";
    private static volatile String hdr = "";
    private static volatile String audioList = "", textList = "";
    private static int generation;
    private static boolean live, hasMedia, settling, errored, audioUnsup;
    private static final List<int[]> audioRefs = new ArrayList<>(), textRefs = new ArrayList<>(); // {group index, track index}
    private static int curAudio = -1;
    private static float volume = 1f;

    private OriginPlayer() {}

    static void attach(OriginActivity a) {
        activity = a;
        a.videoView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(@NonNull final SurfaceHolder holder) {
                post(new Runnable() { public void run() { if (player != null) player.setVideoSurface(holder.getSurface()); } });
            }
            @Override public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {}
            @Override public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
                // the player must let go of the surface before Android frees it
                final CountDownLatch done = new CountDownLatch(1);
                if (!post(new Runnable() { public void run() { if (player != null) player.clearVideoSurface(); done.countDown(); } })) return;
                try { done.await(500, TimeUnit.MILLISECONDS); } catch (InterruptedException ignored) {}
            }
        });
    }

    private static boolean post(Runnable r) { return h != null && h.post(r); }

    // ---- called from C (the SDL thread) ----------------------------------------------------------------

    public static void init() {
        if (thread != null) return;
        thread = new HandlerThread("origin-player");
        thread.start();
        h = new Handler(thread.getLooper());
        post(new Runnable() { public void run() { create(); } });
    }

    public static void load(final String url) {
        post(new Runnable() { public void run() {
            if (player == null) return;
            reset();
            hasMedia = true;
            player.setMediaItem(MediaItem.fromUri(Uri.parse(url)));
            player.prepare();
            player.setPlayWhenReady(true);
            publish();
        }});
    }

    public static void unload() {
        post(new Runnable() { public void run() {
            if (player == null) return;
            player.stop();
            player.clearMediaItems();
            reset();
            publish();
            if (activity != null) activity.keepScreenOn(false);
        }});
    }

    public static void play() { post(new Runnable() { public void run() { if (player != null) { player.setPlayWhenReady(true); publish(); } } }); }

    public static void pause() { post(new Runnable() { public void run() { if (player != null) { player.setPlayWhenReady(false); publish(); } } }); }

    public static void seek(final double sec) {
        post(new Runnable() { public void run() {
            if (player == null) return;
            settling = true;
            player.seekTo((long) (sec * 1000));
            publish();
        }});
    }

    public static void volume(final int pct, final int easeMs) {
        post(new Runnable() { public void run() {
            if (player == null) return;
            final float from = volume, to = Math.max(0, Math.min(100, pct)) / 100f;
            final long t0 = System.currentTimeMillis(), dur = Math.max(0, easeMs);
            Runnable step = new Runnable() { public void run() {
                if (player == null) return;
                float k = dur == 0 ? 1f : Math.min(1f, (System.currentTimeMillis() - t0) / (float) dur);
                volume = from + (to - from) * k;
                player.setVolume(volume);
                if (k < 1f) h.postDelayed(this, 30);
            }};
            step.run();
        }});
    }

    public static void selectAudio(final int idx) {
        post(new Runnable() { public void run() { select(C.TRACK_TYPE_AUDIO, audioRefs, idx); curAudio = idx; publish(); } });
    }

    public static void selectText(final int idx) {
        post(new Runnable() { public void run() {
            if (player == null) return;
            if (idx < 0) {
                player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon()
                        .setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true).build());
                return;
            }
            select(C.TRACK_TYPE_TEXT, textRefs, idx);
        }});
    }

    public static void window(int x, int y, int w, int h) { if (activity != null) activity.placeVideo(x, y, w, h); }

    /** state, pos, dur, buffered end, width, height, fps, live, has media, audio unsupported, settling, current audio, track generation */
    public static double[] poll() { return snap; }

    public static String error() { return error; }

    public static String tracks(int kind) { return kind == 0 ? audioList : textList; }

    public static String hdr() { return hdr; }

    static void release() {
        post(new Runnable() { public void run() {
            if (player != null) { player.release(); player = null; }
            thread.quitSafely();
            thread = null;
            h = null;
        }});
        activity = null;
    }

    // ---- on the player thread -----------------------------------------------------------------------------

    private static void create() {
        if (activity == null) return;
        DefaultRenderersFactory renderers = new DefaultRenderersFactory(activity)
                .setEnableDecoderFallback(true)
                .setExtensionRendererMode(DefaultRenderersFactory.EXTENSION_RENDERER_MODE_PREFER);
        DefaultHttpDataSource.Factory http = new DefaultHttpDataSource.Factory()
                .setAllowCrossProtocolRedirects(true)
                .setConnectTimeoutMs(15000)
                .setReadTimeoutMs(30000)
                .setUserAgent("StremioOrigin (Android TV; native)");
        DefaultTrackSelector selector = new DefaultTrackSelector(activity);
        selector.setParameters(selector.buildUponParameters()
                .setPreferredAudioLanguage(Locale.getDefault().getLanguage())
                .setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true)); // the app shows its own subtitles unless it picks one inside the file
        player = new ExoPlayer.Builder(activity, renderers)
                .setLooper(Looper.myLooper())
                .setTrackSelector(selector)
                .setMediaSourceFactory(new DefaultMediaSourceFactory(new DefaultDataSource.Factory(activity, http)))
                .setHandleAudioBecomingNoisy(true)
                .build();
        if (activity.videoView.getHolder().getSurface() != null && activity.videoView.getHolder().getSurface().isValid()) {
            player.setVideoSurface(activity.videoView.getHolder().getSurface());
        }
        player.addListener(new Player.Listener() {
            @Override public void onPlaybackStateChanged(int s) {
                if (s == Player.STATE_READY) settling = false;
                publish();
            }
            @Override public void onIsPlayingChanged(boolean playing) {
                if (activity != null) activity.keepScreenOn(playing);
                publish();
            }
            @Override public void onRenderedFirstFrame() { live = true; publish(); }
            @Override public void onPositionDiscontinuity(@NonNull Player.PositionInfo a, @NonNull Player.PositionInfo b, int reason) {
                if (reason == Player.DISCONTINUITY_REASON_SEEK && player.getPlaybackState() == Player.STATE_READY) settling = false;
                publish();
            }
            @Override public void onPlayerError(@NonNull PlaybackException e) {
                errored = true;
                error = e.getErrorCodeName() + (e.getMessage() != null ? ": " + e.getMessage() : "");
                Log.w(TAG, "[video] " + error, e);
                publish();
            }
            @Override public void onTracksChanged(@NonNull Tracks t) { tracksChanged(t); publish(); }
            @Override public void onVideoSizeChanged(@NonNull VideoSize v) { publish(); }
            @Override public void onCues(@NonNull final CueGroup cues) {
                final OriginActivity a = activity;
                if (a != null && a.subtitles != null) a.runOnUiThread(new Runnable() { public void run() { a.subtitles.setCues(cues.cues); } });
            }
        });
        h.post(new Runnable() { public void run() { publish(); if (h != null) h.postDelayed(this, 100); } }); // position and buffer, ten times a second
    }

    private static void reset() {
        live = false; hasMedia = false; settling = false; errored = false; audioUnsup = false;
        error = ""; hdr = ""; curAudio = -1;
        audioRefs.clear(); textRefs.clear(); audioList = ""; textList = ""; generation++;
    }

    private static void tracksChanged(Tracks t) {
        audioRefs.clear(); textRefs.clear();
        StringBuilder a = new StringBuilder(), s = new StringBuilder();
        int cur = -1; boolean selectedSupported = true;
        List<Tracks.Group> groups = t.getGroups();
        for (int gi = 0; gi < groups.size(); gi++) {
            Tracks.Group g = groups.get(gi);
            int type = g.getType();
            if (type != C.TRACK_TYPE_AUDIO && type != C.TRACK_TYPE_TEXT) continue;
            for (int ti = 0; ti < g.length; ti++) {
                Format f = g.getTrackFormat(ti);
                boolean audio = type == C.TRACK_TYPE_AUDIO;
                List<int[]> refs = audio ? audioRefs : textRefs;
                int idx = refs.size();
                refs.add(new int[] { gi, ti });
                if (audio && g.isTrackSelected(ti)) { cur = idx; selectedSupported = g.isTrackSupported(ti); }
                String lang = f.language != null ? f.language : "";
                String name = f.label != null ? f.label : lang;
                String codec = codecName(f.sampleMimeType != null ? f.sampleMimeType : f.codecs);
                int ch = f.channelCount == Format.NO_VALUE ? 0 : f.channelCount;
                int forced = (f.selectionFlags & C.SELECTION_FLAG_FORCED) != 0 ? 1 : 0;
                (audio ? a : s).append(idx).append('\t').append(clean(lang)).append('\t').append(clean(name)).append('\t')
                        .append(clean(codec)).append('\t').append(ch).append('\t').append(forced).append('\n');
            }
        }
        audioList = a.toString(); textList = s.toString();
        if (cur >= 0) curAudio = cur;
        audioUnsup = !selectedSupported;
        generation++;
        Format v = player.getVideoFormat();
        hdr = hdrOf(v);
    }

    private static void select(int type, List<int[]> refs, int idx) {
        if (player == null || idx < 0 || idx >= refs.size()) return;
        int[] r = refs.get(idx);
        List<Tracks.Group> groups = player.getCurrentTracks().getGroups();
        if (r[0] >= groups.size()) return;
        TrackGroup tg = groups.get(r[0]).getMediaTrackGroup();
        player.setTrackSelectionParameters(player.getTrackSelectionParameters().buildUpon()
                .setTrackTypeDisabled(type, false)
                .setOverrideForType(new TrackSelectionOverride(tg, r[1]))
                .build());
    }

    private static void publish() {
        double[] s = new double[13];
        if (player != null) {
            int st = player.getPlaybackState();
            int vs;
            if (errored) vs = ERROR;
            else if (!hasMedia || st == Player.STATE_IDLE) vs = hasMedia ? LOADING : IDLE;
            else if (st == Player.STATE_ENDED) vs = ENDED;
            else if (st == Player.STATE_BUFFERING) vs = live ? BUFFERING : LOADING;
            else vs = player.getPlayWhenReady() ? PLAYING : PAUSED;
            s[0] = vs;
            s[1] = Math.max(0, player.getCurrentPosition()) / 1000.0;
            long d = player.getDuration();
            s[2] = d == C.TIME_UNSET || d < 0 ? 0 : d / 1000.0;
            s[3] = Math.max(0, player.getBufferedPosition()) / 1000.0;
            VideoSize v = player.getVideoSize();
            s[4] = v.width; s[5] = v.height;
            Format f = player.getVideoFormat();
            s[6] = f != null && f.frameRate > 0 ? f.frameRate : 0;
            s[7] = live ? 1 : 0;
            s[8] = hasMedia && st != Player.STATE_IDLE ? 1 : 0;
            s[9] = audioUnsup ? 1 : 0;
            s[10] = settling ? 1 : 0;
            s[11] = curAudio;
        }
        s[12] = generation;
        snap = s;
    }

    private static String hdrOf(Format v) {
        if (v == null) return "";
        if (MimeTypes.VIDEO_DOLBY_VISION.equals(v.sampleMimeType)) return "DolbyVision";
        ColorInfo c = v.colorInfo;
        if (c == null) return "none";
        if (c.colorTransfer == C.COLOR_TRANSFER_ST2084) return "HDR10";
        if (c.colorTransfer == C.COLOR_TRANSFER_HLG) return "HLG";
        return "none";
    }

    private static String codecName(String mime) {
        if (mime == null) return "";
        String m = mime.toLowerCase(Locale.ROOT);
        if (m.contains("dts")) return m.contains("hd") ? "dts-hd" : "dts";
        if (m.contains("eac3") || m.contains("ec-3")) return "eac3";
        if (m.contains("ac3") || m.contains("ac-3")) return "ac3";
        if (m.contains("true-hd") || m.contains("truehd")) return "truehd";
        if (m.contains("mp4a") || m.contains("aac")) return "aac";
        if (m.contains("opus")) return "opus";
        if (m.contains("vorbis")) return "vorbis";
        if (m.contains("flac")) return "flac";
        if (m.contains("mpeg")) return "mp3";
        int slash = m.indexOf('/');
        return slash >= 0 ? m.substring(slash + 1) : m;
    }

    private static String clean(String s) { return s == null ? "" : s.replace('\t', ' ').replace('\n', ' '); }
}
