FROM gcc as build
WORKDIR /

RUN apt-get update && apt-get install cmake -y

COPY . /fsim

RUN cd /fsim/extern \
  && mkdir -p slang-dist \
  && cd slang-dist \
  && wget https://github.com/MikePopoloski/slang/releases/download/v0.8/slang-linux.tar.gz \
  && tar xzf slang-linux.tar.gz --strip-components 1

RUN cd /fsim &&  mkdir -p build \
  && cd build \
  && cmake .. -DCMAKE_BUILD_TYPE=Release \
  && make fsim-bin fsim-runtime -j`(nproc)`


FROM gcc

LABEL description="fsim docker image"
LABEL maintainer="keyi@cs.stanford.edu"

RUN mkdir -p /fsim/lib
RUN mkdir -p /fsim/bin
RUN mkdir -p /fsim/include

COPY --from=build /fsim/extern/marl/include/marl /fsim/include/marl
COPY --from=build /fsim/extern/logic/include/logic /fsim/include/logic
COPY --from=build /fsim/src/runtime/ /fsim/include/runtime
COPY --from=build /fsim/build/tools/fsim /fsim/bin/fsim
COPY --from=build /fsim/build/src/runtime/libfsim-runtime.so /fsim/lib/

RUN  apt-get update && apt-get install -y --no-install-recommends ninja-build \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /work

ENTRYPOINT ["/fsim/bin/fsim"]
