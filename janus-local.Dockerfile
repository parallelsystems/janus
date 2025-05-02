# Builds the Janus Gateway application in a slim alpine container. 
# Platforms: Has been tested on Mac ARM and Linux x86 + ARM.
#
# Build with: docker build --no-cache -t janus -f janus.Dockerfile .
# Run with: `docker run -p 0.0.0.0:8188:8188 -p 8088:8088 janus` for basic API testing
# Run with: `docker run -d --network host janus` on Linux to expose all UDP ports for WebRTC
FROM public.ecr.aws/docker/library/alpine:3.17.0 as base

# Install common dependencies
RUN apk add --no-cache \
    glib \
    jansson \
    libconfig \
    libnice \
    libsrtp \
    libwebsockets \
    libmicrohttpd \
    libcurl

FROM base as builder

ENV JANUS_VERSION=1.1.3
ENV SRC_DIR=/home/src

# Install build dependencies
RUN apk add --no-cache \
    glib-dev \
    zlib-dev \
    pkgconfig \
    jansson-dev \
    libconfig-dev \
    libnice-dev \
    openssl-dev \
    libsrtp-dev \
    autoconf \
    automake \
    libtool \
    build-base \
    libwebsockets-dev \
    libmicrohttpd-dev

# Get source code
# RUN mkdir -p ${SRC_DIR} && cd ${SRC_DIR} \
#     && wget https://github.com/meetecho/janus-gateway/archive/refs/tags/v${JANUS_VERSION}.tar.gz \
#     && tar -xf v${JANUS_VERSION}.tar.gz

RUN mkdir -p ${SRC_DIR}/janus && cd ${SRC_DIR}/janus
WORKDIR ${SRC_DIR}/janus
COPY . .

# Build janus from source and install it to /usr/local
# WORKDIR ${SRC_DIR}/janus-gateway-${JANUS_VERSION}
RUN sh autogen.sh
RUN ./configure \
    --disable-rabbitmq \
    --disable-mqtt \
    --disable-all-plugins \
    --enable-plugin-videoroom \
    --enable-static
RUN make && make install && make configs

# Runtime image
FROM base

# Copy Janus install directory
COPY --from=builder /usr/local /usr/local

# Overwrite the sample video plugin config with a minimal config (no default rooms)
# plugin config documented at https://janus.conf.meetecho.com/docs/videoroom
RUN echo "general: {}" > /usr/local/etc/janus/janus.plugin.videoroom.jcfg

# Setup the broader janus config
COPY janus.jcfg /usr/local/etc/janus/janus.jcfg

# Expose port explicitly for use by Testcontainers
EXPOSE 8188
EXPOSE 8088

# Run the server
# ENTRYPOINT ["/usr/local/bin/janus"]
# Default server argument to set STUN variable. Negate by passing an benign argument
# i.e. `docker run janus -o` "-o disables colors in logging"
# CMD [ "--stun-server=stun.l.google.com:19302" ]
ENTRYPOINT ["/bin/sh"]