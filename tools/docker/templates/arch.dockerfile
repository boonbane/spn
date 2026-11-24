FROM archlinux:latest
RUN pacman -Syu --noconfirm --needed {{.packages}} && pacman -Scc --noconfirm
