#!/usr/bin/env python3
"""Extract firmware blocks that have no hardware dependency so they can be unit-tested on a PC."""
import os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(root, "src", "main.cpp"), encoding="utf-8").read()
out = os.path.join(root, "tests", "build")
os.makedirs(out, exist_ok=True)

def cut(a, b, keep_end=False):
    i = src.index(a)
    j = src.index(b, i) + (len(b) if keep_end else 0)
    return src[i:j]

lunar = cut("// ---- LUNAR BEGIN ----", "// ---- LUNAR END ----", True)
logic = "\n".join([
    cut("void updateEffectiveBrightness(bool force) {", "void drawWaiting() {"),
    cut("static void handlePhoneTime(JsonDocument &d) {", "bool applyHudPayload(const String &payload) {"),
    cut("// ---------------- Hardware button (BOOT / GPIO0) ----------------", "void loop() {"),
])
open(os.path.join(out, "lunar_block.cpp"), "w", encoding="utf-8").write(lunar)
open(os.path.join(out, "fw_logic_part.cpp"), "w", encoding="utf-8").write(logic)
print("extracted: lunar %d bytes, logic %d bytes" % (len(lunar), len(logic)))
