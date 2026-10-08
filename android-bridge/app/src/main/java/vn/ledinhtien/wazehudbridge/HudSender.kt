package vn.ledinhtien.wazehudbridge

import android.content.Context
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

data class HudPacket(
    var turn: String? = null,
    var distanceM: Int? = null,
    var road: String? = null,
    var speed: Int? = null,
    var speedLimit: Int? = null,
    var remainingKm: Double? = null,
    var eta: String? = null,
    var route: String? = null,
    var alertType: String? = null,
    var alertDistanceM: Int? = null
)

object HudSender {
    @Volatile private var latest = HudPacket()
    @Volatile private var lastSend = 0L

    fun mergeAndSend(context: Context, packet: HudPacket) {
        synchronized(this) {
            packet.turn?.let { latest.turn = it }
            packet.distanceM?.let { latest.distanceM = it }
            packet.road?.let { latest.road = it }
            packet.speed?.let { latest.speed = it }
            packet.speedLimit?.let { latest.speedLimit = it }
            packet.remainingKm?.let { latest.remainingKm = it }
            packet.eta?.let { latest.eta = it }
            packet.route?.let { latest.route = it }
            packet.alertType?.let { latest.alertType = it }
            packet.alertDistanceM?.let { latest.alertDistanceM = it }
        }
        send(context, latest)
    }

    fun send(context: Context, packet: HudPacket) {
        val now = System.currentTimeMillis()
        if (now - lastSend < 180) return
        lastSend = now

        val obj = JSONObject()
        packet.turn?.let { obj.put("turn", it) }
        packet.distanceM?.let { obj.put("distance_m", it) }
        packet.road?.let { obj.put("road", it) }
        packet.speed?.let { obj.put("speed", it) }
        packet.speedLimit?.let { obj.put("speed_limit", it) }
        packet.remainingKm?.let { obj.put("remaining_km", it) }
        packet.eta?.let { obj.put("eta", it) }
        packet.route?.let { obj.put("route", it) }

        if (packet.alertType != null || packet.alertDistanceM != null) {
            val a = JSONObject()
            packet.alertType?.let { a.put("type", it) }
            packet.alertDistanceM?.let { a.put("distance_m", it) }
            obj.put("alert", a)
        }

        if (BleHudClient.sendJson(obj.toString())) return

        // Try to restore a previously paired BLE connection for future packets.
        BleHudClient.connectSaved(context.applicationContext)

        // HTTP remains as a fallback and for development/testing.
        val ip = context.getSharedPreferences("hud", Context.MODE_PRIVATE)
            .getString("ip", "")?.trim().orEmpty()
        if (ip.isBlank()) return

        thread(name = "HudSenderHttpFallback") {
            try {
                val c = (URL("http://$ip/hud").openConnection() as HttpURLConnection).apply {
                    requestMethod = "POST"
                    connectTimeout = 1200
                    readTimeout = 1200
                    doOutput = true
                    setRequestProperty("Content-Type", "application/json")
                }
                c.outputStream.use { it.write(obj.toString().toByteArray()) }
                c.inputStream.close()
                c.disconnect()
            } catch (_: Exception) {
            }
        }
    }
}
