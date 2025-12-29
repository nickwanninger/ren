#/usr/bin/env bash


export WAYLAND_DISPLAY=
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/xauth_PnibNW


renderdoccmd capture -w -d . $@
