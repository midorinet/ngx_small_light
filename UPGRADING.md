# Enabling AVIF support in nginx_small_light
By default, nginx_small_light does not support AVIF image format. This document will tell you what changes are made to the codebase (either nginx or nginx_small_light) to enable AVIF support in nginx_small_light.

## Modifications
### Modifying Nginx
Following line in **`auto/cc/clang`**, **`auto/cc/gcc`**, and **`auto/cc/icc`** had been commented out to disable error when warning is detected during compilation.
##### **`auto/cc/clang` / `auto/cc/gcc` / `auto/cc/icc`**
```sh
// ######## 
// ## Some code is omitted.
// ########

CFLAGS="$CFLAGS -Werror" <== Comment out this line

// ######## 
// ## Some code is omitted.
// ########
```

### Modifying ngx_small_light configuration
Modify **`config.in`** to include the correct path for ImageMagick header file.
##### [config.in](./config.in)
```bash
ngx_feature_incs="#include <wand/MagickWand.h>"
```
to
```bash
ngx_feature_incs="#include <MagickWand/MagickWand.h>"
```

### Modify ngx_small_light codebase
#### 1. Resize and Crop
Because the function **MagickTransformImage()** that can do both resize and crop using a single function call is not supported in ImageMagick 7, we need to modify the code to use **MagickResizeImage()** and **MagickCropImage()** instead.
##### [ngx_http_small_light_image.c](./src/ngx_http_small_light_imagemagick.c)
```c
if (sz.scale_flg != 0) {
   p = ngx_snprintf(crop_geo, sizeof(crop_geo) - 1, "%f!x%f!+%f+%f", sz.sw, sz.sh, sz.sx, sz.sy);
   *p = '\0';
   p = ngx_snprintf(size_geo, sizeof(size_geo) - 1, "%f!x%f!",       sz.dw, sz.dh);
   *p = '\0';
   ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "crop_geo:%s", crop_geo);
   ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "size_geo:%s", size_geo);
   trans_wand = MagickTransformImage(ictx->wand, (char *)crop_geo, (char *)size_geo); <== This is the line that needs to be modified
   if (trans_wand == NULL || trans_wand == ictx->wand) {
      r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
      DestroyString(of_orig);
      return NGX_ERROR;
   }
   DestroyMagickWand(ictx->wand);
   ictx->wand = trans_wand;
}
```
to
```c
if (sz.scale_flg != 0) {
   /* crop */
   status = MagickCropImage(ictx->wand, sz.sw, sz.sh, sz.sx, sz.sy); <== Modification result
   if (status == MagickFalse) {
      r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
      DestroyString(of_orig);
      return NGX_ERROR;
   }
   /* scale */
   status = MagickResizeImage(ictx->wand, sz.dw, sz.dh, LanczosFilter); <== Modification result
   if (status == MagickFalse) {
      r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
      DestroyString(of_orig);
      return NGX_ERROR;
   }
}
```

#### Check Background Fill

#### Embed Icon
Because the function **MagickCompositeImageChannel()** is not supported in ImageMagick 7, we need to modify the code to use **MagickCompositeImage()** instead. In ImageMagick7 almost all image processing algorithms are now channel aware and all method channel analogs have been removed (e.g. **MagickCompositeImageChannel()**), they are no longer necessary. [Reference](https://imagemagick.org/script/porting.php)
##### [ngx_http_small_light_image.c](./src/ngx_http_small_light_imagemagick.c)
```c
embedicon = NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "embedicon");
if (ctx->material_dir->len > 0 && ngx_strlen(embedicon) > 0) {
   
   // ######## 
   // ## Some code is omitted.
   // ########

   MagickCompositeImageChannel(ictx->wand, AllChannels, icon_wand, OverCompositeOp, sz.ix, sz.iy); <== This is the line that needs to be modified
   DestroyMagickWand(icon_wand);
}
```
to 
```c
embedicon = NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "embedicon");
if (ctx->material_dir->len > 0 && ngx_strlen(embedicon) > 0) {
   
   // ######## 
   // ## Some code is omitted.
   // ########
   
   MagickCompositeImage(ictx->wand, icon_wand, OverCompositeOp, MagickFalse, sz.ix, sz.iy); <== Modification result
   DestroyMagickWand(icon_wand);
}
```

#### Adding support for AVIF conversion
In order to add support for AVIF conversion, we need to add the following code to some of the file in the codebase.

##### **[ngx_http_small_light_module.h](./src/ngx_http_small_light_module.h)**
Define the constant for AVIF and add it to the list of supported image type and add some limitation for AVIF convertion.
```c
// ######## 
// ## Some code is omitted.
// ########

#define NGX_HTTP_SMALL_LIGHT_IMAGE_WEBP 4
#define NGX_HTTP_SMALL_LIGHT_IMAGE_AVIF 5 <== Add this line

#define NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_WEBP 16383

#define NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF 65536 <== Add this line

#define NGX_HTTP_SMALL_LIGHT_PARAM_GET(hash, k) \
    ngx_hash_find(hash, ngx_hash_key_lc((u_char *)k, ngx_strlen(k)), (u_char *)k, ngx_strlen(k))

// ######## 
// ## Some code is omitted.
// ########
```

##### **[ngx_http_small_light_module.c](./src/ngx_http_small_light_module.c)**
Add AVIF to the list of supported image type and to the list of returned header type.
```c
// ######## 
// ## Some code is omitted.
// ########

const char *ngx_http_small_light_image_types[] = {
    "image/jpeg",
    "image/gif",
    "image/png",
    "image/webp",
    "image/avif"  <== Add this line
};

const char *ngx_http_small_light_image_exts[] = {
    "jpeg",
    "gif",
    "png",
    "webp",
    "avif"  <== Add this line
};

// ######## 
// ## Some code is omitted.
// ########
```
##### **[ngx_http_small_light_type.c](./src/ngx_http_small_light_type.c)**
Add type checking for AVIF, if the type string received is "avif", then return NGX_HTTP_SMALL_LIGHT_IMAGE_AVIF.
```c
ngx_int_t ngx_http_small_light_type(const char *of)
{
    ngx_int_t type;

    if (strcmp(of, "jpeg") == 0 || strcmp(of, "jpg") == 0) {
        type = NGX_HTTP_SMALL_LIGHT_IMAGE_JPEG;
    } else if (strcmp(of, "gif") == 0) {
        type = NGX_HTTP_SMALL_LIGHT_IMAGE_GIF;
    } else if (strcmp(of, "png") == 0) {
        type = NGX_HTTP_SMALL_LIGHT_IMAGE_PNG;
    } else if (strcmp(of, "webp") == 0) {
        type = NGX_HTTP_SMALL_LIGHT_IMAGE_WEBP;
    } else if (strcmp(of, "avif") == 0) {  <=== Add this line
        type = NGX_HTTP_SMALL_LIGHT_IMAGE_AVIF; <=== Add this line
    } else {
        type = NGX_HTTP_SMALL_LIGHT_IMAGE_NONE;
    }

    return type;
}
```

##### **`ngx_http_small_light_imagemagick.c`**
Add AVIF checking and convertion
```c
```

#### Adding library implementation for AVIF conversion
##### **[ngx_http_small_light_avif.c](./src/ngx_http_small_light_avif.c)**
Add MAGICKCORE_HEIC_DELEGATE checking to make sure that the ImageMagick library is compiled with libheif support. Eventhough the ImageMagick library is compiled with libheif support, it is still possible that the libheif library are not installed with AVIF encoder/decoder support such as libaom. In this case, the AVIF conversion will fail. 

Checking encoder/decoder availability through C API is hard, some of the possible solution is by using [GetCoderInfo()](https://imagemagick.org/api/MagickCore/coder_8c_source.html#l00248) or [GetMagickInfo()](https://imagemagick.org/api/MagickCore/magick_8c_source.html#l00091). However,
**GetCoderInfo()** can't be used because the function to initiate/de-initiate Coder Component (**CoderComponentGenesis()** and **CoderComponentTerminus()**) are private function that is not accessible
[header file](https://imagemagick.org/api/MagickCore/coder-private_8h_source.html) . The other solution is to use [GetMagickInfo()](https://imagemagick.org/api/MagickCore/magick_8c_source.html#l00091).
```c
// ######## 
// ## Some code is omitted.
// ########

#if defined(MAGICKCORE_WEBP_DELEGATE)
                ictx->type = type;
#else
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "WebP is not supported %s:%d",
                            __FUNCTION__,
                            __LINE__);
            of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
#endif
            }
// ############################
// ######## Start adding AVIF checking and conversion.
        } else if (type == NGX_HTTP_SMALL_LIGHT_IMAGE_AVIF) {
            if (
                iw > NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF ||
                ih > NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF
            ) {
                of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
                ngx_log_error(
                    NGX_LOG_WARN, r->connection->log, 0,
                    "width=%f or height=%f is too large for avif transformation, MAX SIZE AVIF : %d. Resetting type to %s",
                    iw, ih,
                    NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF,
                    of
                );
            } else {
#if defined(MAGICKCORE_HEIC_DELEGATE)
                ictx->type = type;
#else
                ngx_log_error(
                    NGX_LOG_ERR, r->connection->log, 0,
                    "AVIF is not supported %s:%d",
                    __FUNCTION__,
                    __LINE__);
                of = (char *)ngx_http_small_light_image_exts[ictx->type - 1];
#endif
            }
// ######## END adding AVIF checking and conversion.
// ############################
        } else {
            ictx->type = type;
        }
        
        // Even if the library supports the format, the encoder/decoder may not be available.
        status = MagickSetFormat(ictx->wand, of);
        if (status == MagickFalse) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                            "failed to set format(%s) %s:%d",
                            of,
                            __FUNCTION__,
                            __LINE__);
            MagickSetFormat(ictx->wand, of_orig);
            ctx->of = ctx->inf;
        } else {
            ctx->of = ngx_http_small_light_image_types[ictx->type - 1];
        }

// ######## 
// ## Some code is omitted.
// ########
```

#### Removing some unused variable
##### **[ngx_http_small_light_imagemaigck.c](./src/ngx_http_small_light_imagemagick.c)**
```c
ngx_http_small_light_imagemagick_ctx_t *ictx;
    ngx_http_small_light_image_size_t       sz;
    MagickBooleanType                       status;
    int                                     rmprof_flg, progressive_flg, cmyk2rgb_flg;
    double                                  iw, ih, q;
    char                                   *unsharp, *sharpen, *blur, *of, *of_orig;
    MagickWand                             *canvas_wand, *canvas_bg_wand, *source_wand; <== *trans_wand is removed
    DrawingWand                            *border_wand;
    PixelWand                              *bg_color, *canvas_color, *border_color;
    GeometryInfo                            geo;
    ngx_fd_t                                fd;
    MagickWand                             *icon_wand;
    u_char                                 *p, *embedicon;
    size_t                                  embedicon_path_len, embedicon_len, sled_image_size;
    ngx_int_t                               type;
    u_char                                  jpeg_size_opt[32], embedicon_path[256]; <== crop_geo and size_geo are removed
    ColorspaceType                          color_space;
    Image                                   *wand_image; <== Add new variable
    CacheView                               *image_view; <== Add new variable
    ExceptionInfo                           *exception; <== Add new variable
```