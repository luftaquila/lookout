// =======================================
// Hemisphere observatory-dome cover
// - Planar opening (single plane cut)
// - Arc-shaped foot following the rim (no 3 discrete feet)
// - Opening ratio adjustable by variable
// =======================================
d = 23.5;
r = d/2;

t = 1.6;
clearance = 0.3;
$fn = 200;

// ---- Opening control ----
// open_ratio: fraction of full circumference that is OPEN (0..0.9)
// e.g. 0.33 => ~1/3 open, ~2/3 covered
open_ratio = 0.25;

// direction the opening faces (deg). 0 => +X
open_center_deg = 0;

// ---- Arc-foot (skirt) settings ----
// Make a wider-diameter arc that sits on the base plane (z=0) and follows the rim.
foot_radial = 2.0;     // how far outward the foot extends (mm)
foot_thick  = 1.2;     // radial thickness of the arc (mm) (within foot_radial region)
foot_h      = 1.4;     // height upward from z=0 (mm)
foot_gap    = 0.0;     // small gap above base if desired (mm), keep 0 for flush
foot_overlap = 0.2;

// Optional small base pad thickness (only under covered region). Set 0 to disable.
band_h = 0.0;          // set 0.8 if you want extra glue area under covered arc

inner_r = r + clearance;
outer_r = inner_r + t;

// Derived angles
open_angle   = 360 * open_ratio;
cover_angle  = 360 - open_angle;

// Planar cut location for desired open_angle along rim:
// removed span = 2*acos(c/R)  -> c = R*cos(open_angle/2)
c = outer_r * cos(open_angle/2);

// ------------------ Geometry helpers ------------------
module hemi_sphere(rad){
  intersection() {
    sphere(r=rad);
    translate([0,0,rad/2]) cube([2*rad, 2*rad, rad], center=true); // z in [0, rad]
  }
}

module hemi_shell(){
  difference(){
    hemi_sphere(outer_r);
    hemi_sphere(inner_r);
  }
}

// Remove everything with x > c (after rotating to opening direction)
module planar_opening_cut(){
  big = outer_r * 10;
  translate([c, -big, -big]) cube([big, 2*big, 2*big], center=false);
}

// Keep ONLY the covered region in XY by intersecting with half-space x <= c
module keep_covered_region(child_h){
  // complement of the opening cut (keep x <= c)
  big = outer_r * 10;
  intersection(){
    children();
    // keep x <= c : big block to the left
    translate([-big, -big, -big]) cube([big + c, 2*big, 2*big], center=false);
  }
}

// Arc segment helper: create an annular sector (ring wedge) in XY, extruded in Z
module annular_sector(r_in, r_out, ang_start, ang_end, h, z0=0){
  // Works best when ang_end > ang_start and span <= 360
  linear_extrude(height=h, center=false)
    difference(){
      polygon(points=sector_points(r_out, ang_start, ang_end, 160));
      polygon(points=sector_points(r_in,  ang_start, ang_end, 160));
    }
  translate([0,0,z0]) children();
}

// Generate polygon points for a sector (fan) at radius R
function sector_points(R, a0, a1, n=80) =
  concat([[0,0]],
         [for(i=[0:n]) [R*cos(a0 + (a1-a0)*i/n), R*sin(a0 + (a1-a0)*i/n)]]);

// ------------------ Parts ------------------

// Dome shell with planar opening
module dome(){
  difference(){
    hemi_shell();
    rotate([0,0,open_center_deg]) planar_opening_cut();
  }
}

// Optional base band only under covered region (and NOT under opening)
module base_band_under_cover(){
  if (band_h > 0){
    // Make a ring then cut away opening with same plane
    difference(){
      difference(){
        cylinder(h=band_h, r=outer_r);
        translate([0,0,-0.01]) cylinder(h=band_h+0.02, r=inner_r);
      }
      rotate([0,0,open_center_deg]) planar_opening_cut();
    }
  }
}

// Arc-foot: annular sector OUTSIDE the rim, only on covered side.
// Implemented by taking an annulus and trimming it with the *same planar rule*,
// but using a slightly larger radius so it becomes a wider diameter arc.
module arc_foot(){
  rf_in  = outer_r - foot_overlap;     // was: outer_r
  rf_out = outer_r + foot_radial;

  difference(){
    difference(){
      translate([0,0,foot_gap]) cylinder(h=foot_h, r=rf_out);
      translate([0,0,foot_gap-0.01]) cylinder(h=foot_h+0.02, r=rf_in);
    }
    rotate([0,0,open_center_deg]) planar_opening_cut();
  }
}

// ------------------ Build ------------------
union(){
  dome();
  base_band_under_cover();
  arc_foot();
}
