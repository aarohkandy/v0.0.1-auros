# The real programs the compositor has to draw: Firefox (from ferry-firefox:1,
# which has Mozilla's build), a GTK4 app, a Qt app, and what it takes to build
# aurshell (the same Ubuntu 24.04 the image is). Built by tools/wltest-apps.sh.
FROM ferry-firefox:1
ENV DEBIAN_FRONTEND=noninteractive
RUN for i in 1 2 3; do timeout 300 apt-get update -q && \
      timeout 600 apt-get install -y -q --no-install-recommends \
        gcc libc6-dev pkg-config libwayland-dev libxkbcommon-dev xkb-data libdbus-1-dev \
        fonts-dejavu-core fonts-liberation \
        gtk-4-examples adwaita-icon-theme \
        qterminal qt6-wayland \
        mpv && break; sleep 10; done; \
    rm -rf /var/lib/apt/lists/*
# Qt: qterminal is Qt5 (needs qtwayland5); featherpad and pcmanfm-qt are Qt6.
RUN for i in 1 2 3; do timeout 300 apt-get update -q && \
      timeout 600 apt-get install -y -q --no-install-recommends qtwayland5 featherpad && break; sleep 10; done; \
    rm -rf /var/lib/apt/lists/*
# The file manager the image ships (profiles/desktop.profile): drag and drop.
RUN for i in 1 2 3; do timeout 300 apt-get update -q && \
      timeout 600 apt-get install -y -q --no-install-recommends thunar && break; sleep 10; done; \
    rm -rf /var/lib/apt/lists/*
# X11-only programs: Xwayland, a few small X clients, and xprop/xwininfo to look at what the window manager did.
RUN for i in 1 2 3; do timeout 300 apt-get update -q && \
      timeout 600 apt-get install -y -q --no-install-recommends xwayland xterm x11-apps x11-utils x11-xserver-utils && break; sleep 10; done; \
    rm -rf /var/lib/apt/lists/*
