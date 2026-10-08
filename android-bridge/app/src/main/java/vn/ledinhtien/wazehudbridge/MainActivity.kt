package vn.ledinhtien.wazehudbridge

import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.widget.*
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = getSharedPreferences("hud", MODE_PRIVATE)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 32, 32, 32)
        }

        val title = TextView(this).apply {
            text = "Waze HUD Bridge"
            textSize = 26f
        }

        val ip = EditText(this).apply {
            hint = "ESP32 IP, e.g. 192.168.1.50"
            setText(prefs.getString("ip", "192.168.4.1"))
        }

        val save = Button(this).apply {
            text = "Save ESP32 IP"
            setOnClickListener {
                prefs.edit().putString("ip", ip.text.toString().trim()).apply()
                Toast.makeText(this@MainActivity, "Saved", Toast.LENGTH_SHORT).show()
            }
        }

        val notification = Button(this).apply {
            text = "Enable notification access"
            setOnClickListener {
                startActivity(Intent("android.settings.ACTION_NOTIFICATION_LISTENER_SETTINGS"))
            }
        }

        val accessibility = Button(this).apply {
            text = "Enable accessibility"
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            }
        }

        val test = Button(this).apply {
            text = "Send test HUD"
            setOnClickListener {
                prefs.edit().putString("ip", ip.text.toString().trim()).apply()
                HudSender.send(
                    this@MainActivity,
                    HudPacket(
                        turn = "right",
                        distanceM = 350,
                        road = "Vo Nguyen Giap",
                        speed = 62,
                        speedLimit = 60,
                        remainingKm = 8.6,
                        eta = "10:42",
                        route = "QL1A",
                        alertType = "camera",
                        alertDistanceM = 500
                    )
                )
                Toast.makeText(this@MainActivity, "Test sent", Toast.LENGTH_SHORT).show()
            }
        }

        root.addView(title)
        root.addView(ip)
        root.addView(save)
        root.addView(notification)
        root.addView(accessibility)
        root.addView(test)
        setContentView(root)
    }
}
