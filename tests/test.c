#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdint.h>
#include "greatest.h"
#include "../arithmetic_codec.h"

enum {num_elements = 20};

TEST adaptive_model(void)
{
    struct adaptive_data_model* model = adaptive_data_model_init(16);
    struct arithmetic_codec* codec = ac_init();

    const uint32_t data[num_elements] = {0, 0, 15, 15, 15, 15, 3, 3, 2, 1, 15, 15, 15, 15, 15, 0, 0, 0, 8, 3};
    const uint8_t reference_compressed_data[9] = {0x0, 0xff, 0xf7,0x33, 0x28, 0x66, 0xe6, 0x3, 0x1f};
    uint8_t buffer[256];

    ac_set_buffer(codec, 256, (uint8_t*)buffer);
    ac_start_encoder(codec);

    for(uint32_t i=0; i<num_elements; ++i)
    {
        ac_encode_adaptive(codec, data[i], model);
    }

    uint32_t compressed_size = ac_stop_encoder(codec);
    uint8_t* compressed_buffer = ac_get_buffer(codec);

    ASSERT_EQ(9, compressed_size);

    for(uint32_t i=0; i<compressed_size; ++i)
        ASSERT_EQ_FMT(reference_compressed_data[i], compressed_buffer[i], "%x");
        

    ac_set_buffer(codec, compressed_size, compressed_buffer);
    ac_start_decoder(codec);

    adaptive_data_model_reset(model);

    for(uint32_t i=0; i<num_elements; ++i)
    {
        uint32_t value = ac_decode_adaptive(codec, model);
        ASSERT_EQ_FMT(data[i], value, "%d");
    }

    ac_stop_decoder(codec);
    ac_terminate(codec);
    adaptive_data_model_terminate(model);

    PASS();
}

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN();

    RUN_TEST(adaptive_model);

    GREATEST_MAIN_END();
}

