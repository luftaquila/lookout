$fn = 120;

// -------------------- View --------------------
show = "exploded"; // "assembly" | "base" | "lid" | "exploded"

// -------------------- Requirements --------------------
pir_lens_d = 23.0;
pir_lens_clearance = 0.15;
pir_lens_hole_d = pir_lens_d + pir_lens_clearance;

wallplane_w = 24.0;
wallplane_h = 33.0;
wallplane_clearance = 0.6;
wp_w = wallplane_w + wallplane_clearance;
wp_h = wallplane_h + wallplane_clearance;

depth_req = 15.0;
pocket_depth = depth_req;

// -------------------- Angles --------------------
diag_angle_out = 45;   // lens hole cutter
diag_angle_in  = 225;  // inward frame (+X is inward)

// -------------------- Thickness / Fit --------------------
wall_t = 2.0;
base_t = 2.0;
top_t  = 2.0;

fit_gap = 0.25;
inner_offset = wall_t + fit_gap;

// pocket starts after diagonal wall thickness
wall_keep = wall_t;

// -------------------- Fastening (M3 self-tap) --------------------
screw_d       = 3.0;
screw_clear_d = 3.4;
screw_tap_d   = 2.5;

boss_wall_min = 2.0;
boss_base_clearance = 0.5;

screw_to_outer_wall = screw_d/2 + 0.5;
screw_to_inner      = screw_d/2 + boss_wall_min;

boss_w = screw_to_outer_wall + screw_to_inner;
boss_l = boss_w;

// -------------------- Relay fence --------------------
relay_w = 7.3;
relay_l = 14.8;
relay_fence_t = 0.8;
relay_fence_h = 3.0;
relay_clearance = 0.3;

relay_fence_inner_w = relay_w + relay_clearance;
relay_fence_inner_l = relay_l + relay_clearance;
relay_fence_outer_w = relay_fence_inner_w + 2*relay_fence_t;
relay_fence_outer_l = relay_fence_inner_l + 2*relay_fence_t;

// -------------------- Base holes --------------------
toggle_hole_d = 5.3;
wiring_hole_d = 3.4;

// -------------------- Z placement (from 33mm window) --------------------
bottom_clear = 2.0;
top_clear    = 2.0;

sensor_z    = base_t + bottom_clear + wp_h/2 + 15;
inner_z     = (sensor_z + wp_h/2 + top_clear) - base_t;
lid_skirt_h = inner_z;
outer_z     = inner_z + base_t + top_t;

// -------------------- diag_cut sizing --------------------
diag_cut_user = 10.0;
diag_edge_margin = 2.0;

diag_cut_min_hole   = (pir_lens_hole_d + 2*wall_t + diag_edge_margin) / sqrt(2);
diag_cut_min_window = (wp_w          + 2*wall_t + diag_edge_margin) / sqrt(2);
diag_cut = max(diag_cut_user, diag_cut_min_hole, diag_cut_min_window);

// -------------------- Minimal XY sizing (no breakthrough) --------------------
pocket_shell_margin = 0.6;
min_wall_buffer = wall_t + inner_offset + pocket_shell_margin;

D  = wall_keep + pocket_depth;
W2 = wp_w/2;
reach = (D + W2) / sqrt(2);

// outer_w >= min_wall_buffer + reach + diag_cut/2
outer_w = min_wall_buffer + reach + diag_cut/2;
outer_h = outer_w;

// diagonal wall center
diag_center_x = outer_w - diag_cut/2;
diag_center_y = outer_h - diag_cut/2;

// -------------------- 2D profile --------------------
module diag_profile_2d(w, h, d) {
    polygon(points = [
        [0, 0],
        [w, 0],
        [w, h - d],
        [w - d, h],
        [0, h]
    ]);
}

// -------------------- Cutters --------------------
module pir_lens_hole_cutter() {
    translate([diag_center_x, diag_center_y, sensor_z]) {
        rotate([0,0,diag_angle_out])
        rotate([0,90,0])
            cylinder(h = wall_t*3, d = pir_lens_hole_d, center = true);
    }
}

module wallplane_pocket_cutter() {
    translate([diag_center_x, diag_center_y, sensor_z]) {
        rotate([0,0,diag_angle_in]) {
            translate([wall_keep, -wp_w/2, -wp_h/2])
                cube([pocket_depth, wp_w, wp_h], center=false);
        }
    }
}

// -------------------- 3 Bosses (BL, BR, TL) --------------------
module lid_boss(x0, y0, screw_x, screw_y) {
    boss_bottom_z = boss_base_clearance;
    boss_h = lid_skirt_h - boss_bottom_z;

    translate([x0, y0, boss_bottom_z])
        difference() {
            cube([boss_w, boss_l, boss_h]);
            translate([screw_x, screw_y, -0.1])
                cylinder(h = boss_h + 0.2, d = screw_tap_d);
        }
}

module lid_bosses() {
    // BL
    lid_boss(
        wall_t, wall_t,
        screw_to_outer_wall, screw_to_outer_wall
    );

    // BR
    lid_boss(
        outer_w - wall_t - boss_w, wall_t,
        screw_to_inner, screw_to_outer_wall
    );

    // TL
    lid_boss(
        wall_t, outer_h - wall_t - boss_l,
        screw_to_outer_wall, screw_to_inner
    );
}

// -------------------- Relay fence (avoid all bosses) --------------------
fence_gap = 1.0;

// safe band in Y between BL and TL bosses
safe_y0 = wall_t + boss_l + fence_gap - 5;

relay_fence_x = wall_t + boss_w + fence_gap;
relay_fence_y = safe_y0;

module relay_fence() {
    translate([relay_fence_x, relay_fence_y, base_t]) {
        difference() {
            cube([relay_fence_outer_w, relay_fence_outer_l, relay_fence_h]);
            translate([relay_fence_t, relay_fence_t, -0.1])
                cube([relay_fence_inner_w, relay_fence_inner_l, relay_fence_h + 0.2]);
        }
    }
}

// -------------------- Toggle/Wiring holes placement --------------------
relay_fence_right_x = relay_fence_x + relay_fence_outer_w;
br_boss_left_x      = outer_w - wall_t - boss_w;

holes_center_x = (relay_fence_right_x + br_boss_left_x) / 2;

holes_min_y = wall_t + 2;

wiring_hole_x = holes_center_x;
wiring_hole_y = holes_min_y + wiring_hole_d/2;

toggle_hole_x = holes_center_x;
toggle_hole_y = wiring_hole_y + wiring_hole_d/2 + 5 + toggle_hole_d/2;

// -------------------- Base --------------------
module base_part() {
    // screw centers must match lid bosses
    bl_screw_x = wall_t + screw_to_outer_wall;
    bl_screw_y = wall_t + screw_to_outer_wall;

    br_screw_x = outer_w - wall_t - screw_to_outer_wall;
    br_screw_y = wall_t + screw_to_outer_wall;

    tl_screw_x = wall_t + screw_to_outer_wall;
    tl_screw_y = outer_h - wall_t - screw_to_outer_wall;

    difference() {
        union() {
            linear_extrude(height = base_t)
                diag_profile_2d(outer_w, outer_h, diag_cut);
            relay_fence();
        }

        translate([toggle_hole_x, toggle_hole_y, -0.1])
            cylinder(h = base_t + 0.2, d = toggle_hole_d);

        translate([wiring_hole_x, wiring_hole_y, -0.1])
            cylinder(h = base_t + 0.2, d = wiring_hole_d);

        translate([bl_screw_x, bl_screw_y, -0.1])
            cylinder(h = base_t + 0.2, d = screw_clear_d);

        translate([br_screw_x, br_screw_y, -0.1])
            cylinder(h = base_t + 0.2, d = screw_clear_d);

        translate([tl_screw_x, tl_screw_y, -0.1])
            cylinder(h = base_t + 0.2, d = screw_clear_d);
    }
}

// -------------------- Lid --------------------
module lid_part() {
    union() {
        difference() {
            union() {
                // top plate
                translate([0,0,lid_skirt_h])
                    linear_extrude(height = top_t)
                        diag_profile_2d(outer_w, outer_h, diag_cut);

                // skirt
                linear_extrude(height = lid_skirt_h)
                    difference() {
                        diag_profile_2d(outer_w, outer_h, diag_cut);
                        offset(delta = -(wall_t + fit_gap))
                            diag_profile_2d(outer_w, outer_h, diag_cut);
                    }
            }

            pir_lens_hole_cutter();
            wallplane_pocket_cutter();
        }

        lid_bosses();
    }
}

// -------------------- Views --------------------
module assembly_view() { base_part(); lid_part(); }
module exploded_view() { base_part(); translate([0,0,15]) lid_part(); }

if (show == "base") base_part();
else if (show == "lid") lid_part();
else if (show == "exploded") exploded_view();
else assembly_view();