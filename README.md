# MQTT-gateway
A some/ip service that is responsible for subscribing and publishing to different MQTT topics. other some/ip clients call it via some/ip RPC.

### build mqtt-paho docker image
```bash
$ # build mqtt-paho docker image
$ cd mqtt-paho-docker-build
$ docker build -t mqtt-paho-vsomeip .
```
### build with
```bash
$ docker build -t mqtt_gateway .
```
### run with
```bash
$ docker run --name mqtt_gateway -it --rm --privileged --net host -v /tmp:/tmp:z mqtt_gateway
```