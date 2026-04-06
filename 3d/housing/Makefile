.PHONY: all

all:
	openscad -o lid.stl  -D 'show="lid"'  housing.scad && \
	openscad -o base.stl -D 'show="base"' housing.scad
	openscad -o cover-0.25.stl -D 'open_ratio=0.25' cover.scad && \
	openscad -o cover-0.30.stl -D 'open_ratio=0.30' cover.scad && \
	openscad -o cover-0.35.stl -D 'open_ratio=0.35' cover.scad
