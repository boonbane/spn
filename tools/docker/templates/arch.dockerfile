FROM archlinux:base-20260927.0.600689
RUN echo 'Server = https://archive.archlinux.org/repos/2026/09/27/$repo/os/$arch' > /etc/pacman.d/mirrorlist && pacman -Syu --noconfirm {{.packages}} && pacman -Scc --noconfirm
