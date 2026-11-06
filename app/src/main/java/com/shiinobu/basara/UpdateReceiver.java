package com.shiinobu.basara;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.widget.Toast;

/** Hears what the system did with an update Updater committed. */
public final class UpdateReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        final int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            // The system's install confirmation (and, the first time, "allow from this source").
            final Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) {
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(confirm);
            }
        } else if (status != PackageInstaller.STATUS_SUCCESS) {
            // On success Android replaces the app, which ends this process.
            LauncherActivity.installEnded();
            final String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            Toast.makeText(context, "The update wasn't installed" + (status == PackageInstaller.STATUS_FAILURE_ABORTED
                    ? "." : ": " + (message != null ? message : "status " + status)), Toast.LENGTH_LONG).show();
        }
    }
}
