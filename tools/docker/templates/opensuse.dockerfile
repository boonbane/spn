FROM opensuse/tumbleweed:latest
RUN zypper -n install --no-recommends {{.packages}} && zypper clean -a
