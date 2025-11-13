FROM debian:trixie-slim AS build
SHELL ["/bin/bash", "-c"]

RUN apt-get -y update && apt-get install -y apt-utils && apt-get remove --purge gcc
<<<<<<< HEAD
RUN apt-get install -y -qq -o=Dpkg::Use-Pty=0 clang mold ninja-build cmake libc++-dev libc++abi-dev libcurl4-openssl-dev \
            git curl wget ca-certificates python3 python3-pip
=======
RUN apt-get install -y -qq -o=Dpkg::Use-Pty=0 mold ninja-build cmake clang-18 clang++-18 clang-tools-18 libc++-18-dev libc++abi-18-dev \
            libcurl4-openssl-dev git curl wget ca-certificates python3 python3-pip
>>>>>>> 88cc3915 (Fixed bad parsing when looking for messaging params)
RUN rm $(which gcc) || true
RUN rm $(which g++) || true
RUN rm -rf /var/lib/apt/lists/* && rm /usr/bin/ld
#RUN ln -s $(which clang-18) /usr/bin/clang
#RUN ln -s $(which clang++-18) /usr/bin/clang++
RUN ln -s $(which mold) /usr/bin/ld

COPY . /vcellroot

RUN mkdir -p /vcellroot/build/bin
WORKDIR /vcellroot/build

RUN clang++ --version
RUN /usr/bin/cmake .. -G Ninja -DOPTION_TARGET_MESSAGING=ON -DOPTION_TARGET_DOCS=OFF
RUN ninja --verbose
RUN ctest -VV

WORKDIR /vcellroot/build/bin
ENV PATH="/vcellroot/build/bin:${PATH}"