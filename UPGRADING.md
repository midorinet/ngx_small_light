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

#### Adding MagickCore Initiation
MagickCore are used to set image Alpha. 
##### [ngx_http_small_light_imagemagick.c](./src/ngx_http_small_light_imagemagick.c)
```c

// ######## 
// ## Some code is omitted.
// ########

void ngx_http_small_light_imagemagick_genesis(void)
{
    MagickCoreGenesis(NULL, MagickTrue); <== Add this line
    MagickWandGenesis();
}

void ngx_http_small_light_imagemagick_terminus(void)
{
    MagickCoreTerminus(); <== Add this line
    MagickWandTerminus();
}

// ######## 
// ## Some code is omitted.
// ########

```
#### Check Background Fill
Because the function **MagickResizeImage()** parameter is different between ImageMagick 6 and 7, we need to change the parameter to make it work with ImageMagick 7. The parameter `blurfactor` is removed in ImageMagick In order to change the blur factor, we need to use **MagickSetImageAlpha()** instead. From Imagick 7 and so on, blur is not accessible, instead we need to use Alpha to change the blur factor.
```c
/* create canvas then draw image to the canvas. */
if (sz.cw > 0.0 && sz.ch > 0.0) {
   
   // ######## 
   // ## Some code is omitted.
   // ########

    backgroundfill_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "backgroundfill"));
    if (backgroundfill_flg == 1) {

        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "TEST backgroundfill_flg:%d", backgroundfill_flg);
        // first trim whitespace off the original image
        MagickTrimImage(ictx->wand, 1.0);

        canvas_bg_wand = CloneMagickWand(ictx->wand);
        MagickResizeImage(canvas_bg_wand, sz.cw/4, sz.ch/4, LanczosFilter, 1.0); <== This line needs to be modified
        MagickGaussianBlurImage(canvas_bg_wand, 0, 1);
        MagickResizeImage(canvas_bg_wand, sz.cw*2, sz.ch*2, LanczosFilter, 1.0); <= This line needs to be modified
        MagickResizeImage(canvas_bg_wand, sz.cw*2, sz.ch*2, LanczosFilter);
        MagickSetImageOpacity(canvas_bg_wand, 0.5); <== This line needs to be modified
   
   // ######## 
   // ## Some code is omitted.
   // ########
}
```

to 
```c
/* create canvas then draw image to the canvas. */
if (sz.cw > 0.0 && sz.ch > 0.0) {
   
   // ######## 
   // ## Some code is omitted.
   // ########

    backgroundfill_flg = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "backgroundfill"));
    if (backgroundfill_flg == 1) {

        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "TEST backgroundfill_flg:%d", backgroundfill_flg);
        // first trim whitespace off the original image
        MagickTrimImage(ictx->wand, 1.0);

        canvas_bg_wand = CloneMagickWand(ictx->wand);
        MagickResizeImage(canvas_bg_wand, sz.cw/4, sz.ch/4, LanczosFilter); <== This is modified result
        MagickGaussianBlurImage(canvas_bg_wand, 0, 1);
        MagickResizeImage(canvas_bg_wand, sz.cw*2, sz.ch*2, LanczosFilter); <== This is modified result
        MagickSetImageAlpha(canvas_bg_wand, 0.5);
   
   // ######## 
   // ## Some code is omitted.
   // ########
}
```

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

##### **[ngx_http_small_light_type.c](./src/ngx_http_small_light_type.c)**
In function **ngx_http_small_light_type_detect()**, add the following code to detect AVIF image. AVIF image is identified by the following header: ...ftypavif.... or in hex: 00 00 00 20 66 74 79 70 61 76 69 66 00 00 00 00.
```c
ngx_int_t ngx_http_small_light_type_detect(u_char *image, size_t image_len)
{

// ######## 
// ## Some code is omitted.
// ########

    } else if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x20 &&
               p[4] == 0x66 && p[5] == 0x74 && p[6] == 0x79 && p[7] == 0x70 &&
               p[8] == 0x61 && p[9] == 0x76 && p[10] == 0x69 && p[11] == 0x66 &&
               p[12] == 0x00 && p[13] == 0x00 && p[14] == 0x00 && p[15] == 0x00) {
        return NGX_HTTP_SMALL_LIGHT_IMAGE_AVIF;
    } else {

// ######## 
// ## Some code is omitted.
// ########

}
```
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

#### Adding library implementation for AVIF conversion
##### **[ngx_http_small_light_avif.c](./src/ngx_http_small_light_avif.c)**
Add MAGICKCORE_HEIC_DELEGATE checking to make sure that the ImageMagick library is compiled with libheif support. Eventhough the ImageMagick library is compiled with libheif support, it is still possible that the libheif library are not installed with AVIF encoder/decoder support such as libaom. In this case, the AVIF conversion will fail. 
Checking encoder/decoder availability through C API is hard, some of the possible solution is by using [GetCoderInfo()](https://imagemagick.org/api/MagickCore/coder_8c_source.html#l00248) or [GetMagickInfo()](https://imagemagick.org/api/MagickCore/magick_8c_source.html#l00091). However,
**GetCoderInfo()** can't be used because the function to initiate/de-initiate Coder Component (**CoderComponentGenesis()** and **CoderComponentTerminus()**) are private function that is not accessible
[private](https://imagemagick.org/api/MagickCore/coder-private_8h_source.html). **GetMagickInfo()** somehow make the program disfunctional. So I decided to check if there are any error during the conversion process. 

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

// ############################
// ######## Start adding conversion checking
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
// ######## END adding conversion checking
// ############################

// ######## 
// ## Some code is omitted.
// ########
```

#### Modifying variable declaration
##### **[ngx_http_small_light_imagemagick.c](./src/ngx_http_small_light_imagemagick.c)**
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
```

#### Adding support for Blur that is optimized by size.
Given some command that can be used to blur an image with optimized size, by doing resizing instead of blurring the original image using larger blur radius (sigma). 
```bash
convert test.jpg -resize 10% -background "#fff" -flatten -strip -filter Gaussian \ 
    -unsharp 0.25x0.08+8.3+0.045 -quality 35 -define png:compression-filter=5 \ 
    -define png:compression-level=9 -define png:compression-strategy=1 \
    -define png:exclude-chunk=all  -define filter:sigma=4.5 -resize 1000% o-o-1.jpg
```
The transformations applied to the image are as follows:
1. Resize the image to 10% of its original size.
2. Set the background color to white (#fff).
3. Flatten the image to a single layer.
4. Strip all metadata and comments from the image.
5. Unsharpen the image with the settings 0.25x0.08+8.3+0.045. (radius x sigma + amount + threshold)
6. Set the output quality to 35% (to reduce the size of the image).
7. Set the PNG compression filter to 5, PNG compression level to 9, PNG compression strategy to 1, and exclude all chunks from the PNG output.
8.  Resize the image to 1000% of its size by using gaussian filter with sigma value of 4.5.

Overall, the command resizes, blurs, sharpens, and compresses the image with various settings to achieve blurring with optimized size. To apply that function we will create a new flag called **bluropt**, if the flag was marked as "y" then the function will be applied, otherwise it will be ignored. To apply this change, we need to modify the following files:

##### **[ngx_http_small_light_param.c](./src/ngx_http_small_light_param.c)**
Add bluropt flag to the list of flags and arguments.
```c
// ######## 
// ## Some code is omitted.
// ########
static const ngx_http_small_light_param_t ngx_http_small_light_params[] = {
    // ######## 
    // ## Some code is omitted.
    // ########
    { ngx_string("bluropt"),  "n"}, <== Add this line
    // ######## 
    // ## Some code is omitted.
    // ########
};

static const ngx_str_t ngx_http_small_light_getparams[] = {
    // ######## 
    // ## Some code is omitted.
    // ########
    ngx_string("arg_bluropt"),  <== Add this line
    // ######## 
    // ## Some code is omitted.
    // ########
};

// ######## 
// ## Some code is omitted.
// ########
```

##### **[ngx_http_small_light_module.h](./src/ngx_http_small_light_module.h)**
Add some declarations for bluropt flag such as the minimum image size to apply the resizing mechanism in the function.
```c
// ######## 
// ## Some code is omitted.
// ########
#define NGX_HTTP_SMALL_LIGHT_IMAGE_MAX_SIZE_AVIF 65536

#define NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE 15    <== Add this line

#define NGX_HTTP_SMALL_LIGHT_PARAM_GET(hash, k) \
    ngx_hash_find(hash, ngx_hash_key_lc((u_char *)k, ngx_strlen(k)), (u_char *)k, ngx_strlen(k))
// ######## 
// ## Some code is omitted.
// ########
```

##### **[ngx_http_small_light_imagemagick.c](./src/ngx_http_small_light_imagemagick.c)**
Add the main function for bluropt flag.
```c
// ######## 
// ## Some code is omitted.
// ########
ngx_int_t ngx_http_small_light_imagemagick_process(
    ngx_http_request_t *r, 
    ngx_http_small_light_ctx_t *ctx
) {
    // ######## 
    // ## Some code is omitted.
    // ########

// ############################
// ######## Start adding code here 
    /* optimized blur. */
    bluropt_flag = ngx_http_small_light_parse_flag(NGX_HTTP_SMALL_LIGHT_PARAM_GET_LIT(&ctx->hash, "bluropt"));
    if (bluropt_flag != 0) {
        // Resize image to 10% if image width and height are larger than 15px.
        if (
            iw > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE 
            && ih > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE
        ) {
            status = MagickResizeImage(ictx->wand, iw/10, ih/10, CubicFilter);
            if (status == MagickFalse) {
                r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
                DestroyString(of_orig);
                return NGX_ERROR;
            }
        }
        

        // Set background color.
        bg_color = NewPixelWand();
        PixelSetColor(bg_color, "white");
        MagickSetImageBackgroundColor(ictx->wand, bg_color);

        // Flatten the image to handle if image has multiple layers.
        merge_wand = NewMagickWand();
        merge_wand = MagickMergeImageLayers(ictx->wand, FlattenLayer);
        if (merge_wand == NULL) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        ictx->wand = CloneMagickWand(merge_wand);

        // Strip image from all profiles and comments.
        MagickStripImage(ictx->wand);

        // Unsharp mask.
        status = MagickUnsharpMaskImage(ictx->wand, 0.25, 0.08, 8.3, 0.045);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        
        // Set image compression quality.
        status = MagickSetImageCompressionQuality(ictx->wand, 35);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        
        // Set PNG options.
        MagickSetOption(ictx->wand, "png:compression-filter", "5");
        MagickSetOption(ictx->wand, "png:compression-level", "9");
        MagickSetOption(ictx->wand, "png:compression-strategy", "1");
        MagickSetOption(ictx->wand, "png:exclude-chunk", "all");

        // Blur image using Gaussian blur.
        status = MagickGaussianBlurImage(ictx->wand, 0, 4.5);
        if (status == MagickFalse) {
            r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            DestroyPixelWand(bg_color);
            DestroyMagickWand(merge_wand);
            DestroyString(of_orig);
            return NGX_ERROR;
        }
        
        // Resize image to 1000% if image width and height are larger than 15px.
        if (
            iw > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE 
            && ih > NGX_HTTP_SMALL_LIGHT_MINIMUM_IMAGE_SIZE_BLUR_OPTIMIZE
        ) {
            status = MagickResizeImage(ictx->wand, iw, ih, CubicFilter);
            if (status == MagickFalse) {
                r->err_status = NGX_HTTP_INTERNAL_SERVER_ERROR;
                DestroyPixelWand(bg_color);
                DestroyMagickWand(merge_wand);
                DestroyString(of_orig);
                return NGX_ERROR;
            }
        }
        
        DestroyPixelWand(bg_color);
        DestroyMagickWand(merge_wand);
    }
// ######## Stop adding code here
// ############################

    // ######## 
    // ## Some code is omitted.
    // ########
}
// ######## 
// ## Some code is omitted.
// ########
```

