FROM debian:trixie-slim AS build
SHELL ["/bin/bash", "-c"]

RUN apt-get -y update && apt-get install -y apt-utils && apt-get remove --purge gcc
# line 1 => build dependencies
# line 2 => build tools
# line 3 => dependencies
RUN apt-get install -y -qq -o=Dpkg::Use-Pty=0 \
            git curl wget ca-certificates python3 python3-pip \
            mold ninja-build cmake clang-19 clang++-19 clang-tools-19 libc++-19-dev libc++abi-19-dev \
            libspdlog-dev libcurl4-openssl-dev

RUN rm $(which gcc) || true
RUN rm $(which g++) || true
RUN rm -rf /var/lib/apt/lists/* && rm /usr/bin/ld
RUN ln -s $(which mold) /usr/bin/ld
RUN clang-19 --version
RUN ln -s $(which clang-19) /usr/bin/clang
RUN clang++-19 --version
RUN ln -s $(which clang++-19) /usr/bin/clang++
COPY . /vcellroot

RUN mkdir -p /vcellroot/build/bin
WORKDIR /vcellroot/build

RUN /usr/bin/cmake .. -G Ninja -DOPTION_TARGET_MESSAGING=ON -DOPTION_TARGET_DOCS=OFF -DOPTION_TEST_WITH_LOCALHOST=ON
#RUN /usr/bin/cmake .. -G Ninja -DOPTION_TARGET_MESSAGING=ON -DOPTION_TARGET_DOCS=OFF -DCMAKE_CXX_FLAGS="-fexperimental-library" # `experimental-library` needed for new j-thread feature!
RUN ninja --verbose
RUN ctest -VV --output-on-failure
RUN rm /vcellroot/build/bin/unit_tests

WORKDIR /vcellroot/build/bin
ENV PATH="/vcellroot/build/bin:${PATH}"