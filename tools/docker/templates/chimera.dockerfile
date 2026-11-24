FROM chimeralinux/chimera:latest
RUN apk add --no-cache --no-interactive {{.packages}}
