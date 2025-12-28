#/usr/bin/env bash


export WAYLAND_DISPLAY=
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/xauth_PnibNW


CAPTUREDIR=./rdoc
mkdir -p ${CAPTUREDIR}
renderdoccmd capture -w -d . -c ${CAPTUREDIR} $@
