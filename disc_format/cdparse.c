/*  Copyright (c) 2017 James Laird-Wah
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <endian.h>
#include <stdlib.h>
#include <stdarg.h>
#include "cdparse.h"

#ifdef TEST
    #define dbgprintf printf
    #include "../satiator-types.h"
#else
    #include "satiator.h"
#endif

char *cdparse_error_string = NULL;

void cdparse_set_error(const char *fmt, ...) {
   va_list ap;
   if (cdparse_error_string)
       free(cdparse_error_string);

   char buf[1];
   va_start(ap, fmt);
   int len = vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   cdparse_error_string = malloc(len+1);
   va_start(ap, fmt);
   vsnprintf(cdparse_error_string, len+1, fmt, ap);
   va_end(ap);
}

int iso2desc(const char *infile, const char *outfile) {

    struct stat st;
    int ret = stat(infile, &st);
    if (ret < 0) {
        cdparse_set_error("Could not stat ISO file");
        return -1;
    }
    FILE *out = fopen(outfile, "wb");
    if (!out) {
        cdparse_set_error("Can't open output file");
        return -1;
    }

    uint16_t h_nseg = htole16(1);
    fwrite(&h_nseg, 2, 1, out);

    seg_desc_t desc;
    memset(&desc, 0, sizeof(desc));

    desc.track = 1;
    desc.index = 1;
    desc.q_mode = 0x41;
    desc.start = htole32(150);
    desc.length = htole32(st.st_size / 2048);
    desc.secsize = htole16(2048);
    desc.filename_offset = htole32(2 + sizeof(desc));
    desc.file_offset = htole32(0);

    fwrite(&desc, sizeof(desc), 1, out);

    uint8_t filename_len = strlen(infile);
    fwrite(&filename_len, 1, 1, out);
    fwrite(infile, filename_len, 1, out);

    fclose(out);
    return 0;
}

int image2desc(const char *infile, const char *outfile) {
   if (cdparse_error_string)
      free(cdparse_error_string);
   cdparse_error_string = NULL;

   const char *dot = strrchr(infile, '.');
   if (!dot) {
      cdparse_set_error("Unrecognised file extension - no dot in filename");
      return 1;
   }

   const char *extension = dot + 1;

   if (!strcasecmp(extension, "cue"))
      return cue2desc(infile, outfile);

   if (!strcasecmp(extension, "iso"))
      return iso2desc(infile, outfile);

   cdparse_set_error("Unrecognised file extension '%s'", dot);
   return 1;
}
