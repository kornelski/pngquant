#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "rwpng.h"
#include "libimagequant.h"
#include "pngquant_opts.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2)
    return 0;

  char img[256];
  sprintf(img, "/tmp/libfuzzer.png");

  FILE *fp = fopen(img, "wb");
  if (!fp)
    return 0;
  fwrite(data, size, 1, fp);
  fclose(fp);

  liq_attr *attr = liq_attr_create();
  png24_image tmp = {.width=0};
  liq_image *input_image = NULL;
  read_image(attr, img, false, &tmp, &input_image, true, true, false);
  
  liq_attr_destroy(attr);
  if(input_image!=NULL){
    liq_image_destroy(input_image);
  }
  rwpng_free_image24(&tmp);
  unlink(img);
  return 0; 
}
