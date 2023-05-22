set -o errexit
set -o nounset

# Build ngx_small_light
echo "Building ngx_small_light..."
./setup
if [ ! -f config ]
then
  echo "failed setting up ngx_small_light"
  exit 1
fi
/sbin/ldconfig /usr/local/lib

# Build nginx
echo "Building nginx..."
cd ..
wget "http://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz"
shasum "nginx-${NGINX_VERSION}.tar.gz" | grep "${NGINX_SHA}"
tar -zxf "nginx-${NGINX_VERSION}.tar.gz"
cd "nginx-${NGINX_VERSION}"
# Change the compilation options to avoid error.
sed -i -e 's/-Werror/-Werror -Wno-type-limits/g' ./auto/cc/gcc
sed -i -e 's/-Werror/-Werror -Wno-type-limits/g' ./auto/cc/clang
sed -i -e 's/-Werror/-Werror -Wno-type-limits/g' ./auto/cc/icc
if [ ! -d /ngx_small_light/logs ]
then
  mkdir -p /ngx_small_light/logs
fi
for log in /ngx_small_light/logs/smllght_error.log /ngx_small_light/logs/cdn_error.log /ngx_small_light/logs/cdn_access.log
do
  if [ ! -f $log ]
  then
    echo "Creating $log"
    touch $log
    chmod 666 $log
  fi
done 
./configure \
    --prefix=/opt/nginx \
    --conf-path=/etc/nginx/nginx.conf \
    --sbin-path=/usr/local/sbin/nginx \
    --with-http_stub_status_module \
    --with-http_perl_module \
    --with-pcre \
    --with-http_ssl_module \
    --with-http_gzip_static_module \
    --with-http_perl_module \
    --add-module=/ngx_small_light
make
make install
cd ../ngx_small_light