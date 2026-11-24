FROM fedora:45
RUN dnf install -y --setopt=install_weak_deps=False {{.packages}} && dnf clean all
