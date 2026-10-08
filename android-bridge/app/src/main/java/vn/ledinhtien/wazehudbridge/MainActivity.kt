package vn.ledinhtien.wazehudbridge

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat

class MainActivity : AppCompatActivity() {
    private lateinit var bleStatus: TextView

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { result ->
            val granted = result.values.all { it }
            if (granted) {
                BleHudClient.scanAndConnect(this)
            } else {
                bleStatus.text = "Thiếu quyền Bluetooth/BLE"
            }
        }

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

        val bleInfo = TextView(this).apply {
            text = "BLE truyền dữ liệu HUD · Wi-Fi chỉ dùng Web Setting / OTA"
        }

        bleStatus = TextView(this).apply {
            text = BleHudClient.statusText
            textSize = 16f
            setPadding(0, 20, 0, 10)
        }

        val scan = Button(this).apply {
            text = "Quét & kết nối WAZE-HUD"
            setOnClickListener { requestBleAndScan() }
        }

        val disconnect = Button(this).apply {
            text = "Ngắt BLE"
            setOnClickListener { BleHudClient.disconnect() }
        }

        val ip = EditText(this).apply {
            hint = "ESP32 IP dự phòng, ví dụ 192.168.4.1"
            setText(prefs.getString("ip", "192.168.4.1"))
        }

        val saveIp = Button(this).apply {
            text = "Lưu IP dự phòng"
            setOnClickListener {
                prefs.edit().putString("ip", ip.text.toString().trim()).apply()
                Toast.makeText(this@MainActivity, "Đã lưu IP", Toast.LENGTH_SHORT).show()
            }
        }

        val notification = Button(this).apply {
            text = "Bật quyền đọc thông báo"
            setOnClickListener {
                startActivity(Intent("android.settings.ACTION_NOTIFICATION_LISTENER_SETTINGS"))
            }
        }

        val accessibility = Button(this).apply {
            text = "Bật Accessibility"
            setOnClickListener {
                startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
            }
        }

        val test = Button(this).apply {
            text = "Gửi HUD thử"
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
                Toast.makeText(
                    this@MainActivity,
                    if (BleHudClient.isConnected) "Đã gửi qua BLE" else "Đã gửi thử / dùng HTTP dự phòng",
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

        root.addView(title)
        root.addView(bleInfo)
        root.addView(bleStatus)
        root.addView(scan)
        root.addView(disconnect)
        root.addView(ip)
        root.addView(saveIp)
        root.addView(notification)
        root.addView(accessibility)
        root.addView(test)
        setContentView(root)

        BleHudClient.setStatusListener { status ->
            bleStatus.text = status
        }

        if (hasBlePermissions()) {
            BleHudClient.connectSaved(this)
        }
    }

    override fun onDestroy() {
        BleHudClient.setStatusListener(null)
        super.onDestroy()
    }

    private fun requestBleAndScan() {
        val missing = blePermissions().filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isEmpty()) {
            BleHudClient.scanAndConnect(this)
        } else {
            permissionLauncher.launch(missing.toTypedArray())
        }
    }

    private fun hasBlePermissions(): Boolean = blePermissions().all {
        ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    private fun blePermissions(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
}
