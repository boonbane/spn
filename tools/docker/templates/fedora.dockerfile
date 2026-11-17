FROM fedora:41
RUN dnf install -y {{.packages}} && dnf clean all
