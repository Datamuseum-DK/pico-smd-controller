#!/usr/bin/env bash
set -e
frames="1 2"
crop="500x300+0+0!"
gif_frames=""
for frame in ${frames} ; do
	dst="tmp-${frame}.gif"
	convert ${frame}.png -crop ${crop} -interpolate Nearest -filter point -resize "200%" ${dst}
	gif_frames="$gif_frames $dst"
done
gifsicle -d 100 $gif_frames -l -o final.gif
