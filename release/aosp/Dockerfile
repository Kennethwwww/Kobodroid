FROM ubuntu:18.04
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
    bison build-essential ca-certificates ccache curl e2fsprogs flex g++-multilib \
    gcc-multilib git gnupg gperf libc6-dev-i386 libgl1-mesa-dev libncurses5-dev \
    lib32ncurses5-dev lib32z1-dev libx11-dev libxml2-utils openjdk-8-jdk-headless \
    python python3 rsync unzip xsltproc zip zlib1g-dev && \
    curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o /usr/local/bin/repo && \
    chmod 0755 /usr/local/bin/repo && useradd -m builder
RUN apt-get install -y --no-install-recommends cpio
COPY build.sh check-images.sh raw-system.sh jack-env.sh patch_init.py patch_art_host.py /opt/kobodroid/aosp/
WORKDIR /aosp
