FROM alpine:3.24
# SWAY=glassine: the package repository's sway-glassine (the compositor the session
# runs, with glassine_ink_v1; published for aarch64), else Alpine's stock sway
ARG SWAY=stock
COPY glassine-20261015.rsa.pub /etc/apk/keys/
RUN if [ "$SWAY" = glassine ]; then \
        echo https://pkgs.glassine.org/alpine/v3.24/glassine >> /etc/apk/repositories; S=sway-glassine; \
    else S=sway; fi; \
    apk add --no-cache $S gcc musl-dev pkgconf wayland-dev wayland-protocols gtk+3.0-dev py3-gobject3 \
        python3 font-dejavu >/dev/null
