package vn.ledinhtien.wazehudbridge

import android.accessibilityservice.AccessibilityService
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo

class WazeAccessibilityService : AccessibilityService() {
    private var lastFingerprint = ""
    private var lastAt = 0L

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        event ?: return
        val pkg = event.packageName?.toString()?.lowercase().orEmpty()
        if (!pkg.contains("waze")) return

        val root = rootInActiveWindow ?: return
        val texts = ArrayList<String>(32)
        collect(root, texts, 0)

        val fingerprint = texts.joinToString("|")
        val now = System.currentTimeMillis()
        if (fingerprint == lastFingerprint && now - lastAt < 1800) return
        lastFingerprint = fingerprint
        lastAt = now

        if (texts.isNotEmpty()) {
            HudSender.mergeAndSend(this, WazeParser.parse(texts))
        }
    }

    private fun collect(node: AccessibilityNodeInfo?, out: MutableList<String>, depth: Int) {
        if (node == null || depth > 12 || out.size > 80) return
        node.text?.toString()?.trim()?.takeIf { it.isNotBlank() }?.let { out += it }
        node.contentDescription?.toString()?.trim()?.takeIf { it.isNotBlank() }?.let { out += it }
        for (i in 0 until node.childCount) collect(node.getChild(i), out, depth + 1)
    }

    override fun onInterrupt() {}
}
