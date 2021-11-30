FROM gcc as build
WORKDIR /

RUN apt-get update && apt-get install cmake -y

COPY . /xsim

RUN cd /xsim/extern \
  && mkdir -p slang-dist \
  && cd slang-dist \
  && wget https://github.com/MikePopoloski/slang/releases/download/v0.8/slang-linux.tar.gz \
  && tar xzf slang-linux.tar.gz --strip-components 1

RUN cd /xsim &&  mkdir -p build \
  && cd build \
  && cmake .. -DCMAKE_BUILD_TYPE=Release \
  && make xsim-bin xsim-runtime -j`(nproc)`


FROM gcc

LABEL description="xsim docker image"
LABEL maintainer="keyi@cs.stanford.edu"

RUN mkdir -p /xsim/lib
RUN mkdir -p /xsim/bin
RUN mkdir -p /xsim/include

COPY --from=build /xsim/extern/marl/include/marl /xsim/include/marl
COPY --from=build /xsim/extern/logic/include/logic /xsim/include/logic
COPY --from=build /xsim/src/runtime/ /xsim/include/runtime
COPY --from=build /xsim/build/tools/xsim /xsim/bin/xsim
COPY --from=build /xsim/build/src/runtime/libxsim-runtime.so /xsim/lib/

RUN  apt-get update && apt-get install -y --no-install-recommends ninja-build \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /work

ENTRYPOINT ["/xsim/bin/xsim"]
