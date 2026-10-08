package vn.ledinhtien.wazehudbridge

object WazeParser {
    private val distanceRegex = Regex("""(?i)(\d+(?:[.,]\d+)?)\s*(km|m)\b""")
    private val speedRegex = Regex("""(?i)\b(\d{1,3})\s*km/?h\b""")
    private val etaRegex = Regex("""\b([01]?\d|2[0-3]):[0-5]\d\b""")

    fun parse(texts: List<String>): HudPacket {
        val all = texts.filter { it.isNotBlank() }.joinToString(" | ")
        val low = all.lowercase()
        val packet = HudPacket()

        val distances = distanceRegex.findAll(all).toList()
        distances.firstOrNull()?.let {
            packet.distanceM = toMeters(it.groupValues[1], it.groupValues[2])
        }

        etaRegex.find(all)?.let { packet.eta = it.value }
        speedRegex.find(all)?.let { packet.speed = it.groupValues[1].toIntOrNull() }

        packet.turn = when {
            hasAny(low, "turn right", "rẽ phải", "re phai") -> "right"
            hasAny(low, "turn left", "rẽ trái", "re trai") -> "left"
            hasAny(low, "slight right", "chếch phải", "chech phai") -> "slight_right"
            hasAny(low, "slight left", "chếch trái", "chech trai") -> "slight_left"
            hasAny(low, "u-turn", "quay đầu", "quay dau") -> "uturn"
            hasAny(low, "roundabout", "vòng xuyến", "vong xuyen") -> "roundabout"
            hasAny(low, "continue", "straight", "đi thẳng", "di thang") -> "straight"
            else -> null
        }

        packet.alertType = when {
            hasAny(low, "police", "cảnh sát", "canh sat", "hidden police") -> "police"
            hasAny(low, "speed camera", "camera tốc độ", "camera toc do", "mobile camera") -> "camera"
            hasAny(low, "crash", "accident", "tai nạn", "tai nan") -> "crash"
            hasAny(low, "roadworks", "construction", "công trường", "cong truong") -> "roadworks"
            hasAny(low, "pothole", "ổ gà", "o ga") -> "pothole"
            hasAny(low, "car on shoulder", "vehicle on shoulder", "xe dừng", "xe dung") -> "car_on_shoulder"
            hasAny(low, "broken traffic light", "đèn hỏng", "den hong") -> "broken_light"
            hasAny(low, "closure", "road closed", "đóng đường", "dong duong") -> "closure"
            hasAny(low, "blocked lane", "lane blocked", "làn bị chặn", "lan bi chan") -> "blocked_lane"
            hasAny(low, "hazard", "object on road", "chướng ngại", "chuong ngai") -> "object"
            hasAny(low, "traffic", "kẹt xe", "ket xe", "ùn tắc", "un tac") -> "traffic"
            hasAny(low, "weather", "fog", "flood", "mưa", "mua") -> "bad_weather"
            hasAny(low, "high risk", "danger area", "nguy hiểm", "nguy hiem") -> "high_risk"
            hasAny(low, "animal", "động vật", "dong vat") -> "animal"
            else -> null
        }

        if (packet.alertType != null && distances.size > 1) {
            val d = distances[1]
            packet.alertDistanceM = toMeters(d.groupValues[1], d.groupValues[2])
        }

        // Best-effort road name: prefer a medium-length non-control line.
        packet.road = texts.firstOrNull {
            val s = it.trim()
            s.length in 4..42 &&
                !distanceRegex.containsMatchIn(s) &&
                !etaRegex.containsMatchIn(s) &&
                !speedRegex.containsMatchIn(s)
        }

        return packet
    }

    private fun toMeters(value: String, unit: String): Int {
        val n = value.replace(',', '.').toDoubleOrNull() ?: return 0
        return if (unit.lowercase() == "km") (n * 1000).toInt() else n.toInt()
    }

    private fun hasAny(text: String, vararg terms: String) = terms.any { text.contains(it) }
}
