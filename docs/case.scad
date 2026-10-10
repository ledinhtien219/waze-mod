// Waze HUD – parametric enclosure (UNTESTED starting point). Units: mm.
// Measure your modules and edit the variables below.
part = "base";          // "base" or "lid"

// --- ILI9341 2.4" module (PCB) ---
pcb_w = 71.5;           // PCB width
pcb_h = 52.5;           // PCB height
pcb_t = 1.6;            // PCB thickness
win_w = 49.0;           // visible window width (active area ~48.96)
win_h = 37.0;           // visible window height (active area ~36.72)
win_dx = 0;             // window offset from PCB centre (x)
win_dy = 1.5;           // window offset from PCB centre (y)

// --- ESP32 DevKit V1 ---
esp_l = 52.0;
esp_w = 28.5;
esp_h = 14.0;           // incl. headers + USB connector

// --- Box ---
wall = 2.0;
gap = 0.4;              // clearance
depth_front = 8;        // space between lid and PCB
inner_w = pcb_w + 2*gap;
inner_h = pcb_h + 2*gap + esp_w + 4;   // PCB on top, ESP32 below
inner_d = depth_front + pcb_t + esp_h;
usb_w = 12; usb_h = 7;  // micro-USB cut-out

module rounded(w, h, d, r=3) {
  hull() for (x=[r, w-r], y=[r, h-r]) translate([x, y, 0]) cylinder(r=r, h=d, $fn=32);
}

module base() {
  difference() {
    rounded(inner_w + 2*wall, inner_h + 2*wall, inner_d + wall);
    translate([wall, wall, wall]) rounded(inner_w, inner_h, inner_d + 1, 2);
    // micro-USB opening on the short side, ESP32 compartment
    translate([-1, wall + (esp_w + 4 - usb_w)/2 + gap, wall + 2]) cube([wall + 2, usb_w, usb_h]);
  }
  // four screw posts
  for (x=[wall+4, inner_w+wall-4], y=[wall+4, inner_h+wall-4])
    translate([x, y, wall]) difference() { cylinder(d=6, h=inner_d - 2, $fn=24); cylinder(d=2.4, h=inner_d, $fn=24); }
}

module lid() {
  difference() {
    rounded(inner_w + 2*wall, inner_h + 2*wall, wall);
    // display window, PCB sits in the upper part of the box
    translate([wall + inner_w/2 + win_dx - win_w/2,
               wall + (esp_w + 4) + (pcb_h + 2*gap)/2 + win_dy - win_h/2,
               -1]) cube([win_w, win_h, wall + 2]);
    for (x=[wall+4, inner_w+wall-4], y=[wall+4, inner_h+wall-4])
      translate([x, y, -1]) cylinder(d=3.2, h=wall + 2, $fn=24);
  }
}

if (part == "base") base(); else lid();
