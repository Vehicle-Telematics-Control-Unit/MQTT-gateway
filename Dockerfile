# if you dont access use this 
# FROM vsomeip_build:v0 as builder
FROM registry.digitalocean.com/vehicle-plus/tcu_builder_packs:v8 as builder


RUN apk update && apk add --no-cache --virtual .second_build_dependency \
    binutils cmake curl gcc g++ git libtool make tar build-base linux-headers

# MQTT paho build
RUN apk update && \
    apk add --no-cache openssl openssl-dev

WORKDIR /

RUN git clone https://github.com/eclipse/paho.mqtt.c.git
# RUN cd paho.mqtt.c
WORKDIR /paho.mqtt.c
RUN git checkout v1.3.8
RUN cmake -Bbuild -H. -DPAHO_ENABLE_TESTING=OFF -DPAHO_BUILD_STATIC=ON \
    -DPAHO_WITH_SSL=ON -DPAHO_HIGH_PERFORMANCE=ON
RUN cmake --build build/ --target install

WORKDIR /
RUN git clone https://github.com/eclipse/paho.mqtt.cpp
WORKDIR /paho.mqtt.cpp

RUN cmake -Bbuild -H. -DPAHO_BUILD_STATIC=ON \
    -DPAHO_BUILD_DOCUMENTATION=FALSE -DPAHO_BUILD_SAMPLES=FALSE
RUN cmake --build build/ --target install

WORKDIR /

COPY src_build src

# service build
RUN cd src; \
    rm -rf build; \
    mkdir build; \
    cd build; \
    cmake ..; \
    make

FROM alpine:3.17.2
COPY --from=builder /src/build /src/build
COPY --from=builder /usr/local/lib/libvsomeip3.so.3 /usr/local/lib
COPY --from=builder /usr/local/lib/libvsomeip3-cfg.so.3 /usr/local/lib
COPY --from=builder /usr/local/lib/libvsomeip3-sd.so.3 /usr/local/lib
COPY --from=builder /usr/local/lib/libpaho-mqttpp3.so.1 /usr/local/lib
COPY --from=builder /usr/local/lib/libpaho-mqtt3as.so.1 /usr/local/lib
COPY --from=builder /lib/libssl.so.3 /usr/local/lib
COPY --from=builder /lib/libcrypto.so.3 /usr/local/lib
COPY --from=builder /usr/lib/libstdc++.so.6 /usr/lib
COPY --from=builder /usr/lib/libboost_thread.so.1.63.0 /usr/lib
COPY --from=builder /usr/lib/libboost_system.so.1.63.0 /usr/lib
COPY --from=builder /usr/lib/libgcc_s.so.1 /usr/lib
COPY --from=builder /usr/lib/libboost_filesystem.so.1.63.0 /usr/lib

WORKDIR /src/build

ENTRYPOINT [ "./mqtt_client" ]

# COPY vsomeip.json /etc/vsomeip