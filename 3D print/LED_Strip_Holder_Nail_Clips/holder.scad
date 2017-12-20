/**
 * Cable holder clips.
 *
 * Made by coredump hackerspace in Rapperswil, Switzerland.
 * CC BY-SA 3.0.
 *
 * Originally published at https://www.youmagine.com/designs/led-strip-holder-nail-clips/
 *
 */
corner_radius = 4;
thickness = 5.5;
base_width = 20;
nail_radius = 1;
nail_head_depth = 1;
nail_head_radius = 1.9;
led_strip_width = 10.5;
led_strip_height = 1;

module base_raw() {
	hull() {
		translate([0, - corner_radius])
			circle(r=0.01, $fn=50);
		translate([0, corner_radius])
			circle(r=0.01, $fn=50);
		translate([base_width - corner_radius, 0])
			circle(r=corner_radius, $fn=50);
	};
};

module led_strip() {
	translate([thickness, 0, 0])
		cube([led_strip_width, led_strip_height, thickness]);
};

// Covers the LED strip
module base() {
	difference() {
		linear_extrude(thickness) intersection() {
			base_raw();
			square([999, 999]);
		};
		led_strip();
	};
};

// Nail goes in here
module nail_socket() {
	difference() {
		cube([thickness, thickness, thickness]);
	};
};

// The nail
module nail() {
	union() {
		// Nail
		rotate(a=[270, 0, 0])
			translate([thickness/2, -thickness/2])
			cylinder(h=thickness + 10, r=nail_radius, $fn=50);
		// Nail head
		translate([0, thickness - nail_head_depth])
			rotate(a=[270, 0, 0])
			translate([thickness/2, -thickness/2])
			cylinder(h=nail_head_depth, r=nail_head_radius, $fn=50);
	};
};

// This is the final stuff
difference() {
	union() {
		base();
		nail_socket();
	};
	nail();
}
