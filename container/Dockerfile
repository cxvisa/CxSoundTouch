# soundtouch as one fully static musl binary on scratch, plus a CA bundle, plus nothing else:
# about 2.2 MB unpacked, 1.1 MB to pull.
#
#   make container            (or: docker build -f container/Dockerfile -t soundtouch:static .)
#
# Every library is linked in statically and built with only the features soundtouch uses:
#   libcurl        HTTP + HTTPS only (HTTP/1.1, no HTTP/2 or HTTP/3, no proxy/cookies/auth/...)
#   mbedTLS        TLS for libcurl (Alpine's static archive)
#   libwebsockets  plain ws:// client only (no TLS, no server, no HTTP/2, no extensions)
#   pugixml, nlohmann/json (header-only), libstdc++, libgcc, musl
#
# Run it with host networking (the relay advertises this host's address to the speaker, and
# discovery uses multicast) and a data directory holding streams.json and devices.json, where
# state.json is also written:
#
#   docker run -d --name soundtouch --restart unless-stopped --init --network host \
#       -v ~/soundtouch-data:/data soundtouch:static control --update-track-info
#
# The image runs as uid/gid 1000; with a different owner for the data directory, add
# --user "$(id -u):$(id -g)".

ARG ALPINE_VERSION=3.24

# ---------------------------------------------------------------------------------------------
# Source tarballs, pinned by SHA-256. (BuildKit's "ADD --checksum <url>" got HTTP 500 from GitHub's
# release-asset CDN here, so busybox wget fetches them instead.)
FROM alpine:${ALPINE_VERSION} AS sources

WORKDIR /sources

RUN wget -q -O curl.tar.xz    https://curl.se/download/curl-8.22.0.tar.xz && \
    wget -q -O lws.tar.gz     https://github.com/warmcat/libwebsockets/archive/refs/tags/v4.5.8.tar.gz && \
    wget -q -O pugixml.tar.gz https://github.com/zeux/pugixml/releases/download/v1.16/pugixml-1.16.tar.gz && \
    wget -q -O json.tar.xz    https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz && \
    printf '%s\n' \
        'f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7  curl.tar.xz' \
        'b6ade658f4af3a823d0dc806ae5ef0623f0f4f5e2aeb895a0f77c4783840c30e  lws.tar.gz' \
        '4cee1ca4aad395170f4c7a07824f3bdd41f28316c6e1e1090a1425b278ec0b4b  pugixml.tar.gz' \
        '42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa  json.tar.xz' \
        | sha256sum -c -

# ---------------------------------------------------------------------------------------------
# Toolchain: GCC 15 (Alpine 3.24) for -std=c++26.
FROM alpine:${ALPINE_VERSION} AS toolchain

RUN apk add --no-cache build-base cmake linux-headers pkgconf file ca-certificates-bundle

# Size flags for the libraries: -Os, and one section per function/object so the final link can
# drop everything soundtouch never calls (-Wl,--gc-sections).
ENV DEPS_CFLAGS="-Os -ffunction-sections -fdata-sections"

# ---------------------------------------------------------------------------------------------
# libwebsockets, client-only, plain ws://.
FROM toolchain AS lws

COPY --from=sources /sources/lws.tar.gz /tmp/

RUN tar xzf /tmp/lws.tar.gz -C /tmp && \
    cmake -S /tmp/libwebsockets-4.5.8 -B /tmp/lws-build \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_C_FLAGS="${DEPS_CFLAGS}" \
        -DDISABLE_WERROR=ON \
        -DLWS_WITH_SSL=OFF \
        -DLWS_WITH_SHARED=OFF -DLWS_WITH_STATIC=ON \
        -DLWS_WITHOUT_SERVER=ON \
        -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITH_MINIMAL_EXAMPLES=OFF \
        -DLWS_WITH_HTTP2=OFF \
        -DLWS_IPV6=OFF -DLWS_UNIX_SOCK=OFF -DLWS_WITH_UDP=OFF \
        -DLWS_ROLE_RAW_FILE=OFF \
        -DLWS_CLIENT_HTTP_PROXYING=OFF \
        -DLWS_WITH_HTTP_BASIC_AUTH=OFF -DLWS_WITH_HTTP_DIGEST_AUTH=OFF \
        -DLWS_WITH_HTTP_UNCOMMON_HEADERS=OFF -DLWS_WITH_CUSTOM_HEADERS=OFF \
        -DLWS_WITH_SECURE_STREAMS=OFF \
        -DLWS_WITH_LEJP=OFF -DLWS_WITH_LEJP_CONF=OFF -DLWS_WITH_JSONRPC=OFF \
        -DLWS_WITH_LHP=OFF -DLWS_WITH_DLO=OFF -DLWS_WITH_UPNG=OFF -DLWS_WITH_JPEG=OFF \
        -DLWS_WITH_GZINFLATE=OFF \
        -DLWS_WITH_DIR=OFF -DLWS_WITH_FILE_OPS=OFF \
        -DLWS_WITH_CONMON=OFF -DLWS_WITH_WOL=OFF \
        -DLWS_WITH_NETLINK=OFF -DLWS_WITH_CACHE_NSCOOKIEJAR=OFF \
        -DLWS_WITH_EVLIB_PLUGINS=OFF && \
    cmake --build /tmp/lws-build -j"$(nproc)" && \
    DESTDIR=/staging cmake --install /tmp/lws-build

# ---------------------------------------------------------------------------------------------
# pugixml, static.
FROM toolchain AS pugixml

COPY --from=sources /sources/pugixml.tar.gz /tmp/

RUN tar xzf /tmp/pugixml.tar.gz -C /tmp && \
    cmake -S /tmp/pugixml-1.16 -B /tmp/pugixml-build \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_CXX_FLAGS="${DEPS_CFLAGS}" \
        -DBUILD_SHARED_LIBS=OFF -DPUGIXML_BUILD_TESTS=OFF && \
    cmake --build /tmp/pugixml-build -j"$(nproc)" && \
    DESTDIR=/staging cmake --install /tmp/pugixml-build

# ---------------------------------------------------------------------------------------------
# nlohmann/json, header-only.
FROM toolchain AS json

COPY --from=sources /sources/json.tar.xz /tmp/

RUN mkdir -p /staging/usr/local/include && \
    tar xJf /tmp/json.tar.xz -C /tmp && \
    cp -R /tmp/json/include/nlohmann /staging/usr/local/include/

# ---------------------------------------------------------------------------------------------
# mbedTLS, from Alpine's static archive.
FROM toolchain AS tls

RUN apk add --no-cache mbedtls-dev mbedtls-static

# ---------------------------------------------------------------------------------------------
# libcurl: HTTP and HTTPS, nothing else. The CA bundle path is compiled in and is where the final
# image keeps it.
FROM tls AS curl

COPY --from=sources /sources/curl.tar.xz /tmp/

RUN tar xJf /tmp/curl.tar.xz -C /tmp && \
    cd /tmp/curl-8.22.0 && \
    ./configure CFLAGS="${DEPS_CFLAGS}" \
        --prefix=/usr/local \
        --disable-shared --enable-static \
        --with-mbedtls \
        --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt \
        --without-ca-path --without-ca-fallback --without-ca-embed \
        --enable-http \
        --disable-ftp --disable-file --disable-ipfs --disable-ldap --disable-ldaps \
        --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
        --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt \
        --disable-websockets \
        --disable-proxy \
        --disable-http-auth --disable-basic-auth --disable-bearer-auth --disable-digest-auth \
        --disable-kerberos-auth --disable-negotiate-auth --disable-aws --disable-httpsig \
        --disable-ntlm --disable-sspi --disable-sha512-256 \
        --disable-cookies --disable-alt-svc --disable-hsts --disable-doh --disable-httpsrr \
        --disable-ech --disable-ssls-export --disable-mime --disable-form-api \
        --disable-headers-api --disable-get-easy-options --disable-netrc --disable-dateparse \
        --disable-bindlocal --disable-dnsshuffle --disable-unix-sockets \
        --disable-progress-meter \
        --disable-manual --disable-docs --disable-libcurl-option \
        --enable-threaded-resolver --enable-ipv6 --enable-verbose \
        --without-zlib --without-brotli --without-zstd --without-libpsl --without-libidn2 \
        --without-nghttp2 --without-ngtcp2 --without-nghttp3 --without-quiche \
        --without-libssh --without-libssh2 --without-libgsasl --without-ldap --without-libuv \
        --without-zsh-functions-dir --without-fish-functions-dir \
        > /tmp/curl-configure.log 2>&1 || { cat /tmp/curl-configure.log; exit 1; } ; \
    sed -n '/^configure: Configured to build curl/,$p' /tmp/curl-configure.log && \
    if grep 'unrecognized options' /tmp/curl-configure.log; then exit 1; fi && \
    make -j"$(nproc)" && \
    make install DESTDIR=/staging && \
    ./src/curl -V

# ---------------------------------------------------------------------------------------------
# soundtouch itself, built with its own unchanged Makefile; only the flags are overridden.
FROM tls AS build

COPY --from=lws     /staging/ /
COPY --from=pugixml /staging/ /
COPY --from=json    /staging/ /
COPY --from=curl    /staging/ /

WORKDIR /src
COPY Makefile *.cpp *.h ./

# Alpine's GCC defaults to PIE, so plain -static already yields a static-pie binary. PIE=no
# (default) adds -no-pie for a classic static executable: smaller, fixed load address. PIE=yes
# keeps static-pie (ASLR) at +0.1 MB. Do not pass
# -static-pie explicitly here: with Alpine's specs it leaves a NEEDED entry and segfaults.
ARG PIE=no

# musl gives new threads a 128 KiB stack; glibc gives 8 MiB. musl takes the default from the
# binary's PT_GNU_STACK size, so -z stack-size restores the glibc figure for every std::thread.
RUN case "${PIE}" in yes) LINK="-static" ;; *) LINK="-static -no-pie" ;; esac && \
    make -j"$(nproc)" \
        CXXFLAGS="-std=c++26 -Wall -Wextra -Werror -pedantic-errors -O2 -ffunction-sections -fdata-sections" \
        LDFLAGS="${LINK} -static-libstdc++ -static-libgcc -Wl,--gc-sections -Wl,-z,stack-size=8388608 \
                 $(pkg-config --static --libs libcurl) -lwebsockets -lpugixml" && \
    ls -l cxstcc && \
    strip --strip-all cxstcc && \
    ls -l cxstcc && \
    file cxstcc && \
    file cxstcc | grep -Eq 'statically linked|static-pie linked' && \
    if readelf -d cxstcc | grep -q NEEDED; then echo "dynamic dependency left" >&2; exit 1; fi && \
    mkdir -p /rootfs/data /rootfs/etc/ssl/certs && \
    cp cxstcc /rootfs/cxstcc && \
    cp /etc/ssl/certs/ca-certificates.crt /rootfs/etc/ssl/certs/ca-certificates.crt

# ---------------------------------------------------------------------------------------------
FROM scratch

COPY --from=build /rootfs/cxstcc /cxstcc
COPY --from=build /rootfs/etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
COPY --from=build --chown=1000:1000 /rootfs/data /data

USER 1000:1000
WORKDIR /data

# No STOPSIGNAL: cxstcc handles SIGTERM, docker stop's default, itself, even as PID 1.

ENTRYPOINT ["/cxstcc"]

