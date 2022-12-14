#ifndef _DECODER_H_
#define _DECODER_H_

#include <arib25/arib_std_b25.h>
#include <arib25/b_cas_card.h>

typedef struct decoder {
    ARIB_STD_B25 *b25;
    B_CAS_CARD *bcas;
} decoder;

typedef struct decoder_options {
    int round;
    int strip;
    int emm;
} decoder_options;

/* prototypes */
decoder *b25_startup(decoder_options *opt);
int b25_shutdown(decoder *dec);
int b25_decode(decoder *dec,
               ARIB_STD_B25_BUFFER *sbuf,
               ARIB_STD_B25_BUFFER *dbuf);
int b25_finish(decoder *dec,
               ARIB_STD_B25_BUFFER *sbuf,
               ARIB_STD_B25_BUFFER *dbuf);

#endif
