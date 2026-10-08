package vn.ledinhtien.wazehudbridge

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

class WazeNotificationService : NotificationListenerService() {
    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        sbn ?: return
        if (!sbn.packageName.lowercase().contains("waze")) return

        val e = sbn.notification.extras
        val texts = listOfNotNull(
            e.getCharSequence(Notification.EXTRA_TITLE)?.toString(),
            e.getCharSequence(Notification.EXTRA_TEXT)?.toString(),
            e.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString(),
            e.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString()
        )

        if (texts.isNotEmpty()) {
            HudSender.mergeAndSend(this, WazeParser.parse(texts))
        }
    }
}
