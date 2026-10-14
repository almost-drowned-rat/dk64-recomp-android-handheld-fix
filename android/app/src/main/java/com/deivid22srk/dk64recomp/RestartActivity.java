package com.deivid22srk.dk64recomp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.File;

/**
 * Neustart des Spiels in einem frischen Prozess (Handheld-Fork).
 *
 * Läuft im eigenen Prozess ":restart" (Manifest). MainActivity startet sie,
 * solange sie selbst noch im Vordergrund ist, und beendet sich dann
 * (onDestroy -> killProcess). Diese Activity wartet, bis der alte Prozess
 * weg ist, und öffnet die App neu.
 *
 * Vorher wurde die Wiederöffnung per AlarmManager geplant; ab Android 10
 * blockiert das System diesen Start aus dem Hintergrund ("Abort background
 * activity starts"), die App blieb nach dem Laden der ROM geschlossen. Eine
 * sichtbare (durchsichtige) Activity darf die App dagegen starten.
 */
public class RestartActivity extends Activity {

    private static final String TAG = "DK64Recomp";
    static final String EXTRA_OLD_PID = "old_pid";

    private static final long POLL_MS = 50;
    private static final long TIMEOUT_MS = 5000;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private int oldPid;
    private long deadline;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        oldPid = getIntent().getIntExtra(EXTRA_OLD_PID, -1);
        deadline = System.currentTimeMillis() + TIMEOUT_MS;
        handler.post(this::waitAndRelaunch);
    }

    private void waitAndRelaunch() {
        boolean oldAlive = oldPid > 0 && new File("/proc/" + oldPid).exists();
        if (oldAlive && System.currentTimeMillis() < deadline) {
            handler.postDelayed(this::waitAndRelaunch, POLL_MS);
            return;
        }
        if (oldAlive) {
            Log.w(TAG, "RestartActivity: alter Prozess " + oldPid + " lebt noch, starte trotzdem");
        }
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(launch);
            Log.i(TAG, "RestartActivity: App neu gestartet");
        } else {
            Log.w(TAG, "RestartActivity: kein Launch-Intent — bitte manuell öffnen");
        }
        finish();
        overridePendingTransition(0, 0);
        // Der Hilfsprozess wird nicht mehr gebraucht.
        handler.postDelayed(() -> android.os.Process.killProcess(android.os.Process.myPid()), 500);
    }
}
