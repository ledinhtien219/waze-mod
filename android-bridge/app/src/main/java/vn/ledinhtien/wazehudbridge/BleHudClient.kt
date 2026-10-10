package vn.ledinhtien.wazehudbridge

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Handler
import android.os.Looper
import java.nio.charset.StandardCharsets
import java.util.ArrayDeque
import java.util.UUID

object BleHudClient {
    const val DEVICE_NAME = "WAZE-HUD"
    val SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    val RX_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")

    @Volatile var isConnected: Boolean = false
        private set

    @Volatile var statusText: String = "Chưa kết nối"
        private set

    private var connecting = false
    private var gatt: BluetoothGatt? = null
    private var rxCharacteristic: BluetoothGattCharacteristic? = null
    private var mtu = 23
    private var activeScan: ScanCallback? = null
    private var statusListener: ((String) -> Unit)? = null

    // The ESP32 has no RTC battery: push the phone clock after connecting and every 10 minutes,
    // so the standby clock / auto-dim work without Wi-Fi. {"v":1,"t":"time","ts":<unix seconds>}
    private val timeSyncRunnable = object : Runnable {
        override fun run() {
            if (!isConnected) return
            sendJson("{\"v\":1,\"t\":\"time\",\"ts\":${System.currentTimeMillis() / 1000L}}")
            mainHandler.postDelayed(this, 600_000L)
        }
    }

    private val writeQueue = ArrayDeque<ByteArray>()
    private var writeInProgress = false
    private val mainHandler = Handler(Looper.getMainLooper())

    fun setStatusListener(listener: ((String) -> Unit)?) {
        statusListener = listener
        listener?.invoke(statusText)
    }

    private fun setStatus(value: String) {
        statusText = value
        mainHandler.post { statusListener?.invoke(value) }
    }

    @SuppressLint("MissingPermission")
    fun scanAndConnect(context: Context) {
        val app = context.applicationContext
        val manager = app.getSystemService(BluetoothManager::class.java)
        val adapter = manager.adapter
        if (adapter == null) {
            setStatus("Điện thoại không hỗ trợ Bluetooth")
            return
        }
        if (!adapter.isEnabled) {
            setStatus("Hãy bật Bluetooth rồi quét lại")
            return
        }

        activeScan?.let { adapter.bluetoothLeScanner?.stopScan(it) }
        activeScan = null

        val scanner = adapter.bluetoothLeScanner
        if (scanner == null) {
            setStatus("Không mở được BLE scanner")
            return
        }

        setStatus("Đang quét WAZE-HUD…")

        val callback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val advertisedName = result.scanRecord?.deviceName
                if (advertisedName != DEVICE_NAME) return

                scanner.stopScan(this)
                activeScan = null
                app.getSharedPreferences("hud", Context.MODE_PRIVATE)
                    .edit()
                    .putString("ble_address", result.device.address)
                    .apply()
                connectDevice(app, result.device.address)
            }

            override fun onScanFailed(errorCode: Int) {
                activeScan = null
                setStatus("Quét BLE lỗi: $errorCode")
            }
        }

        activeScan = callback
        val filter = ScanFilter.Builder().setDeviceName(DEVICE_NAME).build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(listOf(filter), settings, callback)

        mainHandler.postDelayed({
            if (activeScan === callback) {
                scanner.stopScan(callback)
                activeScan = null
                setStatus("Không thấy WAZE-HUD")
            }
        }, 10_000)
    }

    @SuppressLint("MissingPermission")
    fun connectSaved(context: Context) {
        if (isConnected || connecting) return
        val address = context.getSharedPreferences("hud", Context.MODE_PRIVATE)
            .getString("ble_address", null)
            ?: return
        connectDevice(context.applicationContext, address)
    }

    @SuppressLint("MissingPermission")
    private fun connectDevice(context: Context, address: String) {
        if (isConnected || connecting) return

        val manager = context.getSystemService(BluetoothManager::class.java)
        val adapter = manager.adapter ?: return
        val device = try {
            adapter.getRemoteDevice(address)
        } catch (_: IllegalArgumentException) {
            setStatus("Địa chỉ BLE đã lưu không hợp lệ")
            return
        }

        connecting = true
        setStatus("Đang kết nối WAZE-HUD…")
        gatt?.close()
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        mainHandler.removeCallbacks(timeSyncRunnable)
        connecting = false
        isConnected = false
        synchronized(writeQueue) {
            writeQueue.clear()
            writeInProgress = false
        }
        rxCharacteristic = null
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        setStatus("Đã ngắt BLE")
    }

    fun sendJson(json: String): Boolean {
        if (!isConnected) return false
        val bytes = (json + "\n").toByteArray(StandardCharsets.UTF_8)
        val payloadSize = (mtu - 3).coerceAtLeast(20).coerceAtMost(180)

        synchronized(writeQueue) {
            var offset = 0
            while (offset < bytes.size) {
                val end = (offset + payloadSize).coerceAtMost(bytes.size)
                writeQueue.addLast(bytes.copyOfRange(offset, end))
                offset = end
            }
            if (!writeInProgress) writeNextLocked()
        }
        return true
    }

    @SuppressLint("MissingPermission")
    private fun writeNextLocked() {
        val currentGatt = gatt
        val characteristic = rxCharacteristic
        if (currentGatt == null || characteristic == null || writeQueue.isEmpty()) {
            writeInProgress = false
            return
        }

        writeInProgress = true
        val chunk = writeQueue.removeFirst()
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        characteristic.value = chunk
        if (!currentGatt.writeCharacteristic(characteristic)) {
            writeInProgress = false
            writeQueue.clear()
            setStatus("Gửi BLE thất bại")
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                connecting = false
                setStatus("Đã kết nối, đang tìm dịch vụ…")
                g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                mainHandler.removeCallbacks(timeSyncRunnable)
                connecting = false
                isConnected = false
                rxCharacteristic = null
                synchronized(writeQueue) {
                    writeQueue.clear()
                    writeInProgress = false
                }
                setStatus("BLE đã ngắt")
                g.close()
                if (gatt === g) gatt = null
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                setStatus("Không đọc được BLE service")
                return
            }

            val service: BluetoothGattService? = g.getService(SERVICE_UUID)
            val rx = service?.getCharacteristic(RX_UUID)
            if (rx == null) {
                setStatus("WAZE-HUD không có RX characteristic")
                g.disconnect()
                return
            }

            rxCharacteristic = rx
            isConnected = true
            setStatus("Đã kết nối WAZE-HUD")
            g.requestMtu(185)
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) mtu = newMtu
            mainHandler.removeCallbacks(timeSyncRunnable)
            mainHandler.postDelayed(timeSyncRunnable, 500L)
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            synchronized(writeQueue) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    writeQueue.clear()
                    writeInProgress = false
                    setStatus("Gửi BLE lỗi: $status")
                    return
                }
                writeInProgress = false
                writeNextLocked()
            }
        }
    }
}
