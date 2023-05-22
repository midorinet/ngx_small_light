FROM debian:buster

# Zero interaction when installing packages via apt-get.
ENV DEBIAN_FRONTEND=noninteractive

# For building ImageMagick and its dependencies.
ARG IM_VERSION=7.1.1-8
ARG LIB_HEIF_VERSION=1.16.1
ARG LIB_AOM_VERSION=3.5.0

# For building nginx.
ENV NGINX_VERSION="1.22.1"
ENV NGINX_SHA="45a89797f7c789287c7f663811efbbd19e84f154"

# Install ImageMagick
RUN apt-get -y update && \
    apt-get -y upgrade && \
    apt-get install -y git make gcc pkg-config autoconf curl g++ cmake clang wget \
    # libaom dependencies
    yasm \
    # libheif dependencies
    libde265-dev libx265-dev x265 libjpeg-dev libtool \
    # libwebp
    libwebp-dev \
    # imagemagick dependencies
    libpng16-16 libpng-dev libgomp1 ghostscript libxml2-dev libxml2-utils libtiff-dev libfontconfig1-dev libfreetype6-dev fonts-dejavu liblcms2-dev && \
    # Building libaom
    git clone -b v${LIB_AOM_VERSION} --depth 1 https://aomedia.googlesource.com/aom && \
    mkdir build_aom && \
    cd build_aom && \
    cmake ../aom/ -DENABLE_TESTS=0 -DBUILD_SHARED_LIBS=1 && make && make install && \
    ldconfig /usr/local/lib && \
    cd .. && \
    rm -rf aom && \
    rm -rf build_aom && \
    # Building libheif
    curl -L https://github.com/strukturag/libheif/releases/download/v${LIB_HEIF_VERSION}/libheif-${LIB_HEIF_VERSION}.tar.gz -o libheif.tar.gz && \
    tar -xzvf libheif.tar.gz && cd libheif-${LIB_HEIF_VERSION}/ && mkdir build && cd build && cmake --preset=release .. && make && make install && cd ../../ \
    ldconfig /usr/local/lib && \
    rm -rf libheif-${LIB_HEIF_VERSION} && rm libheif.tar.gz && \
    # Building ImageMagick
    git clone -b ${IM_VERSION} --depth 1 https://github.com/ImageMagick/ImageMagick.git && \
    cd ImageMagick && \
    ./configure --with-heic=yes --with-webp=yes && \
    make && make install && \
    /sbin/ldconfig /usr/local/lib && \
    rm -rf /ImageMagick

# Install ngx_small_light and nginx
COPY . /ngx_small_light
RUN apt-get install -y libpcre2-dev libpcre3 libpcre3-dev libssl-dev libperl-dev && \
    cd /ngx_small_light && \
    chmod +x ./build.sh && ./build.sh && \
    mkdir -p /var/nginx/cache && \
    cp ./sample/nginx.conf /etc/nginx/nginx.conf

# Clean up
RUN apt-get autoremove -y && \
    apt-get clean && \
    apt-get autoclean && \
    rm -rf /var/lib/apt/lists/*

EXPOSE 8084 8085 443

WORKDIR /ngx_small_light

CMD ["nginx", "-g", "daemon off;"]