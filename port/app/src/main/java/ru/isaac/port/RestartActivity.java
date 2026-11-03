package ru.isaac.port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/*
 * THE GAME STARTED AGAIN AFTER AN IMPORT (the owner's eighth round, 24.09.2026:
 * "the game crashes on import").
 *
 * An import has to end the process outright - a pause would save the saves the
 * game read at start over the files just imported. The next start used to be
 * left to an alarm with a PendingIntent, and Android 14 and later do not let a
 * process that has gone start an activity that way: the old process died and
 * no new one came, which looked exactly like a crash.
 *
 * This activity lives in a process of its own (":restart", AndroidManifest). The
 * game starts it and then ends itself; this one waits until the game's process
 * is really gone - its /proc entry is the proof - and, being the activity on the
 * screen, is allowed to start the game again. Then it leaves.
 */
public class RestartActivity extends Activity {
    static final String EXTRA_PID = "pid";

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        final int pid = getIntent().getIntExtra(EXTRA_PID, -1);
        new Thread(() -> {
            java.io.File proc = new java.io.File("/proc/" + pid);
            for (int i = 0; i < 100 && pid > 0 && proc.exists(); i++) {
                try { Thread.sleep(50); } catch (InterruptedException ignore) { }
            }
            runOnUiThread(() -> {
                Intent again = getPackageManager().getLaunchIntentForPackage(getPackageName());
                if (again != null) {
                    again.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                    startActivity(again);
                }
                finish();
                android.os.Process.killProcess(android.os.Process.myPid());
            });
        }).start();
    }
}
