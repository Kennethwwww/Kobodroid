FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
    bzip2 ca-certificates curl file git lsb-release python3 python-is-python3 sudo unzip xz-utils zip \
    && useradd -m builder && printf 'builder ALL=(ALL) NOPASSWD:ALL\n' > /etc/sudoers.d/chromium-builder
COPY sync.sh /opt/kobodroid/webview/sync.sh
WORKDIR /chromium
