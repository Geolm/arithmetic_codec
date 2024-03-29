#ifndef __ARITHMETIC_CODEC__
#define __ARITHMETIC_CODEC__

// Ported in C from on FastAC by Amir Said's
// https://github.com/richgel999/FastAC

// The only purpose of this program is to demonstrate the basic principles   -
// of arithmetic coding. It is provided as is, without any express or        -
// implied warranty, without even the warranty of fitness for any particular -
// purpose, or that the implementations are correct.                         -
//                                                                           -
// Permission to copy and redistribute this code is hereby granted, provided -
// that this warning and copyright notices are not removed or altered.       -
//                                                                           -
// Copyright (c) 2004 by Amir Said (said@ieee.org) &                         -
//                       William A. Pearlman (pearlw@ecse.rpi.edu)   

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct adaptive_model;
struct static_model;
struct arithmetic_codec;

//----------------------------------------------------------------------------------------------------------------------
// Adaptive data model
//----------------------------------------------------------------------------------------------------------------------

// Initialize the adaptive data model, returns a pointer to the model
struct adaptive_model* adaptive_model_init(uint32_t number_of_symbols);

// Release memory
void adaptive_model_terminate(struct adaptive_model* model);

// Reset the statistics of the model (all symbols counter resetted to one)
void adaptive_model_reset(struct adaptive_model* model);

// Change the number of symbols of the model. It will reset the model
void adaptive_model_set_alphabet(struct adaptive_model* model, uint32_t number_of_symbols);

// Return how many time the symbol has been encoded
uint32_t adaptive_model_get_symbol_count(const struct adaptive_model* model, uint32_t symbol);

//----------------------------------------------------------------------------------------------------------------------
// Static data model
//----------------------------------------------------------------------------------------------------------------------

// Initialize the static data model, returns a pointer to the model
//      number_of_symbols   Number of symbols maximum
//      probability         Pointer to an array of float containing probability. Array must have the size of number_of_symbols
//                          Each float must be [0;1]
struct static_model* static_model_init(uint32_t number_of_symbols, const float *probability);


// Set up the distribution
//      number_of_symbols   Number of symbols maximum
//      probability         Pointer to an array of float containing probability. Array must have the size of number_of_symbols
//                          Each float must be [0;1]
void static_model_set_distribution(struct static_model* model, uint32_t number_of_symbols, const float *probability);


// Release memory
void static_model_terminate(struct static_model* model);

//----------------------------------------------------------------------------------------------------------------------
// Arithmetic Codec
//----------------------------------------------------------------------------------------------------------------------

// Initialize the arithmetic codec, returns a pointer to the codec
// You need to call ac_set_buffer() before starting encode/decode
struct arithmetic_codec* ac_init(void);


// Set the buffer for compressed data
//      max_code_bytes  Maximum size of the buffer in bytes
//      user_buffer     If the pointer to the buffer is NULL, memory will be allocated internally using malloc()
void ac_set_buffer(struct arithmetic_codec* codec, uint32_t max_code_bytes, uint8_t *user_buffer);

// Set the codec to encoding mode
void ac_start_encoder(struct arithmetic_codec* codec);

// Set the codec to decoding mode
void ac_start_decoder(struct arithmetic_codec* codec);

// Stop encoding, return the number of bytes used in the compressed buffer
uint32_t ac_stop_encoder(struct arithmetic_codec* codec);

// Stop the decoder
void ac_stop_decoder(struct arithmetic_codec* codec);

// Store multiple bits of data in the buffer
void ac_put_bits(struct arithmetic_codec* codec, uint32_t data, uint32_t number_of_bits);

// Get multiple bits of data from the buffer, returns the bits
uint32_t ac_get_bits(struct arithmetic_codec* codec, uint32_t number_of_bits);

// Encode data using an adaptive model, the model should be initialized
void ac_encode_adaptive(struct arithmetic_codec* codec, uint32_t data, struct adaptive_model* model);

// Decode the next data from the buffer using an adaptative model
uint32_t ac_decode_adaptive(struct arithmetic_codec* codec, struct adaptive_model* model);

// Encode data using an static model, the model should be initialized
void ac_encode_static(struct arithmetic_codec* codec, uint32_t data, struct static_model* model);

// Decode the next data from the buffer using an static model
uint32_t ac_decode_static(struct arithmetic_codec* codec, struct static_model* model);

// Return a pointer to the compressed buffer
uint8_t* ac_get_buffer(struct arithmetic_codec* codec);

// Return a pointer to the compressed buffer
void ac_terminate(struct arithmetic_codec* codec);

#ifdef __cplusplus
}
#endif

#endif // __ARITHMETIC_CODEC__


//----------------------------------------------------------------------------------------------------------------------
// Implementation
//----------------------------------------------------------------------------------------------------------------------

#ifdef __ARITHMETIC_CODEC__IMPLEMENTATION__

#include <assert.h>

#if !defined(AC_FREE) && !defined(AC_ALLOC)
#include <stdlib.h>
#define AC_FREE(a) free(a)
#define AC_ALLOC(a) malloc(a)
#endif

//-- constants --------------------------------------------------------------------------------------------------------------------
#define AC__MinLength (0x01000000U)         // threshold for renormalization
#define AC__MaxLength (0xFFFFFFFFU)        // maximum AC interval length

// Maximum values for general models
#define DM__LengthShift (15)                    // length bits discarded before mult.
#define DM__MaxCount    (1 << DM__LengthShift)  // for adaptive models


//----------------------------------------------------------------------------------------------------------------------
// Adaptive data model
//----------------------------------------------------------------------------------------------------------------------

struct adaptive_model
{
    uint32_t *distribution, *symbol_count, *decoder_table;
    uint32_t total_count, update_cycle, symbols_until_update;
    uint32_t data_symbols, last_symbol, table_size, table_shift;
};

void adaptive_model_update(struct adaptive_model* model, int from_encoder);

//----------------------------------------------------------------------------------------------------------------------
struct adaptive_model* adaptive_model_init(uint32_t number_of_symbols)
{
    struct adaptive_model* model = (struct adaptive_model*) AC_ALLOC(sizeof(struct adaptive_model));

    model->data_symbols = 0;
    model->distribution = NULL;

    adaptive_model_set_alphabet(model, number_of_symbols);
    
    return model;
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_model_terminate(struct adaptive_model* model)
{
    AC_FREE(model->distribution);
    AC_FREE(model);
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_model_reset(struct adaptive_model* model)
{
    if (model->data_symbols == 0) 
        return;

    // restore probability estimates to uniform distribution
    model->total_count = 0;
    model->update_cycle = model->data_symbols;

    for (uint32_t k = 0; k < model->data_symbols; k++) 
        model->symbol_count[k] = 1;

    adaptive_model_update(model, 0);
    model->symbols_until_update = model->update_cycle = (model->data_symbols + 6) >> 1;
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_model_set_alphabet(struct adaptive_model* model, uint32_t number_of_symbols)
{
    assert(number_of_symbols>1 && (number_of_symbols <= (1 << 11))); // invalid number of data symbols

    if (model->data_symbols != number_of_symbols) 
    {
        // assign memory for data model
        model->data_symbols = number_of_symbols;
        model->last_symbol = model->data_symbols - 1;
        AC_FREE(model->distribution);

        // define size of table for fast decoding
        if (model->data_symbols > 16) 
        {
            uint32_t table_bits = 3;
            while (model->data_symbols > (1U << (table_bits + 2))) ++table_bits;
            model->table_size  = 1 << table_bits;
            model->table_shift = DM__LengthShift - table_bits;
            model->distribution = (uint32_t*) AC_ALLOC(sizeof(uint32_t) * (2 * model->data_symbols+model->table_size+2));
            model->decoder_table = model->distribution + 2 * model->data_symbols;
            assert(model->distribution != NULL);
        }
        else 
        {
            // small alphabet: no table needed
            model->decoder_table = 0;
            model->table_size = model->table_shift = 0;
            model->distribution = (uint32_t*) AC_ALLOC( sizeof(uint32_t) * 2 * model->data_symbols);
        }
        model->symbol_count = model->distribution + model->data_symbols;
        assert(model->distribution != NULL); // cannot assign model memory
    }

    // initialize model
    adaptive_model_reset(model);
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_model_update(struct adaptive_model* model, int from_encoder)
{
    if ((model->total_count += model->update_cycle) > DM__MaxCount) 
    {
        model->total_count = 0;
        for (uint32_t n = 0; n < model->data_symbols; n++)
            model->total_count += (model->symbol_count[n] = (model->symbol_count[n] + 1) >> 1);
    }

    // compute cumulative distribution, decoder table
    uint32_t k, sum = 0, s = 0;
    uint32_t scale = 0x80000000U / model->total_count;

    if (from_encoder || (model->table_size == 0))
    {
        for (k = 0; k < model->data_symbols; k++) 
        {
            model->distribution[k] = (scale * sum) >> (31 - DM__LengthShift);
            sum += model->symbol_count[k];
        }
    }
    else 
    {
        for (k = 0; k < model->data_symbols; k++) 
        {
            model->distribution[k] = (scale * sum) >> (31 - DM__LengthShift);
            sum += model->symbol_count[k];
            uint32_t w = model->distribution[k] >> model->table_shift;
            while (s < w) model->decoder_table[++s] = k - 1;
        }
        model->decoder_table[0] = 0;
        while (s <= model->table_size) model->decoder_table[++s] = model->data_symbols - 1;
    }

    // set frequency of model updates
    model->update_cycle = (5 * model->update_cycle) >> 2;
    uint32_t max_cycle = (model->data_symbols + 6) << 3;
    if (model->update_cycle > max_cycle) 
        model->update_cycle = max_cycle;
    model->symbols_until_update = model->update_cycle;
}

//----------------------------------------------------------------------------------------------------------------------
uint32_t adaptive_model_get_symbol_count(const struct adaptive_model* model, uint32_t symbol)
{
    assert(symbol < model->data_symbols); // invalid data symbols
    assert(model->distribution != NULL); // adaptive model should be initialized

    return model->symbol_count[symbol];
}

//----------------------------------------------------------------------------------------------------------------------
// Static data model
//----------------------------------------------------------------------------------------------------------------------

struct static_model
{
    uint32_t *distribution, *decoder_table;
    uint32_t data_symbols, last_symbol, table_size, table_shift;
};

//----------------------------------------------------------------------------------------------------------------------
struct static_model* static_model_init(uint32_t number_of_symbols, const float *probability)
{
    struct static_model* model = (struct static_model*) AC_ALLOC(sizeof(struct static_model));

    model->data_symbols = 0;
    model->distribution = NULL;

    static_model_set_distribution(model, number_of_symbols, probability);

    return model;
}

//----------------------------------------------------------------------------------------------------------------------
void static_model_set_distribution(struct static_model* model, uint32_t number_of_symbols, const float *probability)
{
    assert(number_of_symbols>1 && (number_of_symbols <= (1 << 11))); // invalid number of data symbols

    if (model->data_symbols != number_of_symbols) 
    {
        // assign memory for data model
        model->data_symbols = number_of_symbols;
        model->last_symbol = model->data_symbols - 1;
        AC_FREE(model->distribution);

        // define size of table for fast decoding
        if (model->data_symbols > 16) 
        {
            uint32_t table_bits = 3;
            while (model->data_symbols > (1U << (table_bits + 2))) 
                ++table_bits;
            model->table_size  = 1 << table_bits;
            model->table_shift = DM__LengthShift - table_bits;
            model->distribution = (uint32_t*) AC_ALLOC(sizeof(uint32_t) * (model->data_symbols+model->table_size+2));
            model->decoder_table = model->distribution + model->data_symbols;
        }
        else 
        {                                  // small alphabet: no table needed
            model->decoder_table = 0;
            model->table_size = model->table_shift = 0;
            model->distribution = (uint32_t*) AC_ALLOC(sizeof(uint32_t) * model->data_symbols);
        }
        assert(model->distribution != NULL);
    }
                                // compute cumulative distribution, decoder table
    uint32_t s = 0;
    float sum = 0.0f, p = 1.0f / (float)(model->data_symbols);

    for (unsigned k = 0; k < model->data_symbols; k++) 
    {
        if (probability) 
            p = probability[k];

        assert(p>=0.f && p<= 1.f);
        
        model->distribution[k] = (uint32_t)(sum * (1 << DM__LengthShift));
        sum += p;

        if (model->table_size == 0) 
            continue;

        uint32_t w = model->distribution[k] >> model->table_shift;
        while (s < w) model->decoder_table[++s] = k - 1;
    }

    if (model->table_size != 0) 
    {
        model->decoder_table[0] = 0;
        while (s <= model->table_size) 
            model->decoder_table[++s] = model->data_symbols - 1;
    }

    assert(sum >= 0.9999f && sum <= 1.001f);
}

//----------------------------------------------------------------------------------------------------------------------
void static_model_terminate(struct static_model* model)
{
    AC_FREE(model->distribution);
    AC_FREE(model);
}

//----------------------------------------------------------------------------------------------------------------------
// Arithmetic Codec
//----------------------------------------------------------------------------------------------------------------------

struct arithmetic_codec
{
    uint8_t *code_buffer, *new_buffer, *ac_pointer;
    uint32_t base, value, length;                     // arithmetic coding state
    uint32_t buffer_size;
    uint32_t mode;     // mode: 0 = undef, 1 = encoder, 2 = decoder
};

//----------------------------------------------------------------------------------------------------------------------
inline static void ac_propagate_carry(struct arithmetic_codec* codec)
{
    uint8_t * p;            
    // carry propagation on compressed data buffer
    for (p = codec->ac_pointer - 1; *p == 0xFFU; p--) 
        *p = 0;
    ++*p;
}

//----------------------------------------------------------------------------------------------------------------------
inline static void ac_renorm_enc_interval(struct arithmetic_codec* codec)
{
    do  // output and discard top byte
    {
        *codec->ac_pointer++ = (uint8_t)(codec->base >> 24);
        codec->base <<= 8;
    } while ((codec->length <<= 8) < AC__MinLength);        // length multiplied by 256
}

//----------------------------------------------------------------------------------------------------------------------
inline static void ac_renorm_dec_interval(struct arithmetic_codec* codec)
{
    do // read least-significant byte
    {
        codec->value = (codec->value << 8) | (uint32_t)(*++codec->ac_pointer);
    } while ((codec->length <<= 8) < AC__MinLength);        // length multiplied by 256
}

//----------------------------------------------------------------------------------------------------------------------
struct arithmetic_codec* ac_init(void)
{
    struct arithmetic_codec* codec = (struct arithmetic_codec*) AC_ALLOC(sizeof(struct arithmetic_codec));

    codec->mode = codec->buffer_size = 0;
    codec->new_buffer = codec->code_buffer = NULL;

    return codec;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_set_buffer(struct arithmetic_codec* codec, uint32_t max_code_bytes, uint8_t *user_buffer)
{
    assert(codec->mode == 0); // cannot set buffer while encoding or decoding

    if (user_buffer != NULL) 
    {
        // user provides memory buffer
        codec->buffer_size = max_code_bytes;
        codec->code_buffer = user_buffer; // set buffer for compressed data
        AC_FREE(codec->new_buffer); // free anything previously assigned              
        codec->new_buffer = NULL;
        return;
    }

    // enough space available in the current buffer
    if (max_code_bytes <= codec->buffer_size) 
        return;

    codec->buffer_size = max_code_bytes; // assign new memory
    AC_FREE(codec->new_buffer);    // free anything previously assigned
    codec->new_buffer = (uint8_t*) AC_ALLOC(codec->buffer_size + 16); // 16 extra bytes
    assert(codec->new_buffer != NULL);
    codec->code_buffer = codec->new_buffer; // set buffer for compressed data
}

//----------------------------------------------------------------------------------------------------------------------
void ac_start_encoder(struct arithmetic_codec* codec)
{
    assert(codec->mode == 0); // cannot start encoder
    assert(codec->buffer_size != 0); // no buffer set
    codec->mode = 1;
    codec->base = 0;
    codec->length = AC__MaxLength;
    codec->ac_pointer = codec->code_buffer;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_start_decoder(struct arithmetic_codec* codec)
{
    assert(codec->mode == 0); // cannot start encoder
    assert(codec->buffer_size != 0); // no buffer set
    codec->mode = 2;
    codec->length = AC__MaxLength;
    codec->ac_pointer = codec->code_buffer + 3;
    codec->value = ((uint32_t)(codec->code_buffer[0]) << 24) |
                   ((uint32_t)(codec->code_buffer[1]) << 16) |
                   ((uint32_t)(codec->code_buffer[2]) <<  8) |
                    (uint32_t)(codec->code_buffer[3]);
}

//----------------------------------------------------------------------------------------------------------------------
uint32_t ac_stop_encoder(struct arithmetic_codec* codec)
{
    assert(codec->mode == 1); // invalid to stop encoder
    codec->mode = 0;

    uint32_t init_base = codec->base;            // done encoding: set final data bytes

    if (codec->length > 2 * AC__MinLength) 
    {
        codec->base  += AC__MinLength;                                     // base offset
        codec->length = AC__MinLength >> 1;             // set new length for 1 more byte
    }
    else 
    {
        codec->base  += AC__MinLength >> 1;                                // base offset
        codec->length = AC__MinLength >> 9;            // set new length for 2 more bytes
    }

    if (init_base > codec->base) 
        ac_propagate_carry(codec);                 // overflow = carry

    ac_renorm_enc_interval(codec);                // renormalization = output last bytes

    uint32_t code_bytes = (uint32_t)(codec->ac_pointer - codec->code_buffer);
    assert(code_bytes <= codec->buffer_size); // code buffer overflow

    return code_bytes;                                   // number of bytes used
}

//----------------------------------------------------------------------------------------------------------------------
void ac_stop_decoder(struct arithmetic_codec* codec)
{
    assert(codec->mode == 2);  // invalid to stop decoder
    codec->mode = 0;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_put_bit(struct arithmetic_codec* codec, uint32_t bit)
{
    assert(codec->mode == 1);  // encoder not initialized

    codec->length >>= 1;    // halve interval
    if (bit) 
    {
        uint32_t init_base = codec->base;
        codec->base += codec->length;   // move base
        if (init_base > codec->base) 
            ac_propagate_carry(codec);  // overflow = carry
    }

    if (codec->length < AC__MinLength) 
        ac_renorm_enc_interval(codec); // renormalization
}

//----------------------------------------------------------------------------------------------------------------------
uint32_t ac_get_bit(struct arithmetic_codec* codec)
{
    assert(codec->mode == 2); //  decoder not initialized   

    codec->length >>= 1;  // halve interval
    uint32_t bit = (codec->value >= codec->length);  // decode bit
    if (bit) 
        codec->value -= codec->length; // move base

    if (codec->length < AC__MinLength) 
        ac_renorm_dec_interval(codec);  // renormalization

    return bit;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_put_bits(struct arithmetic_codec* codec, uint32_t data, uint32_t number_of_bits)
{
    assert(codec->mode == 1);  // encoder not initialized
    assert((number_of_bits > 0) && (number_of_bits < 21));  // invalid number of bits
    assert(data < (1U << number_of_bits)); // invalid data

    uint32_t init_base = codec->base;
    codec->base += data * (codec->length >>= number_of_bits);            // new interval base and length

    if (init_base > codec->base) 
        ac_propagate_carry(codec);                 // overflow = carry

    if (codec->length < AC__MinLength) 
        ac_renorm_enc_interval(codec);        // renormalization
}

//----------------------------------------------------------------------------------------------------------------------
uint32_t ac_get_bits(struct arithmetic_codec* codec, uint32_t number_of_bits)
{
    assert(codec->mode == 2);  // decoder not initialized
    assert((number_of_bits > 0) && (number_of_bits < 21));  // invalid number of bits

    // decode symbol, change length
    unsigned s = codec->value / (codec->length >>= number_of_bits);      

    codec->value -= codec->length * s; // update interval
    if (codec->length < AC__MinLength) 
        ac_renorm_dec_interval(codec); // renormalization

    return s;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_encode_adaptive(struct arithmetic_codec* codec, uint32_t data, struct adaptive_model* model)
{
    assert(codec->mode == 1);  // encoder not initialized
    assert(data < model->data_symbols); // invalid data symbols
    assert(model->distribution != NULL); // adaptive model should be initialized
    
    uint32_t x;
    uint32_t init_base = codec->base;

    // compute products
    if (data == model->last_symbol) 
    {
        x = model->distribution[data] * (codec->length >> DM__LengthShift);
        codec->base   += x; // update interval
        codec->length -= x; // no product needed
    }
    else 
    {
        x = model->distribution[data] * (codec->length >>= DM__LengthShift);
        codec->base   += x; // update interval
        codec->length  = model->distribution[data+1] * codec->length - x;
    }

    if (init_base > codec->base) 
        ac_propagate_carry(codec);                 // overflow = carry

    if (codec->length < AC__MinLength) 
        ac_renorm_enc_interval(codec);        // renormalization

    ++model->symbol_count[data];
    if (--model->symbols_until_update == 0)
        adaptive_model_update(model, 1);
        

}

//----------------------------------------------------------------------------------------------------------------------
uint32_t ac_decode_adaptive(struct arithmetic_codec* codec, struct adaptive_model* model)
{
    assert(codec->mode == 2); // decoder not initialized
    assert(model->distribution != NULL); // adaptive model should be initialized

    uint32_t n, s, x, y = codec->length;

    if (model->decoder_table) 
    {
        // use table look-up for faster decoding
        uint32_t dv = codec->value / (codec->length >>= DM__LengthShift);
        uint32_t t = dv >> model->table_shift;

        s = model->decoder_table[t];         // initial decision based on table look-up
        n = model->decoder_table[t+1] + 1;

        while (n > s + 1) 
        {                        // finish with bisection search
            uint32_t m = (s + n) >> 1;
            if (model->distribution[m] > dv) 
                n = m; 
            else s = m;
        }

        // compute products
        x = model->distribution[s] * codec->length;
        if (s != model->last_symbol) 
            y = model->distribution[s+1] * codec->length;
    }
    else 
    {
        // decode using only multiplications
        x = s = 0;
        codec->length >>= DM__LengthShift;
        uint32_t m = (n = model->data_symbols) >> 1;

        // decode via bisection search
        do 
        {
            uint32_t z = codec->length * model->distribution[m];
            if (z > codec->value) 
            {
                n = m;
                y = z;                                             // value is smaller
            }
            else 
            {
                s = m;
                x = z;                                     // value is larger or equal
            }
        } while ((m = (s + n) >> 1) != s);
    }

    codec->value -= x;                                               // update interval
    codec->length = y - x;

    if (codec->length < AC__MinLength) 
        ac_renorm_dec_interval(codec);        // renormalization

    ++model->symbol_count[s];
    if (--model->symbols_until_update == 0) 
        adaptive_model_update(model, 0);

    return s;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_encode_static(struct arithmetic_codec* codec, uint32_t data, struct static_model* model)
{
    assert(codec->mode == 1);   // encoder not initialized
    assert(data < model->data_symbols); // invalid data symbol

    uint32_t x, init_base = codec->base;

    // compute products
    if (data == model->last_symbol) 
    {
        x = model->distribution[data] * (codec->length >> DM__LengthShift);
        codec->base   += x;  // update interval
        codec->length -= x;  // no product needed
    }
    else 
    {
        x = model->distribution[data] * (codec->length >>= DM__LengthShift);
        codec->base   += x;                                            // update interval
        codec->length  = model->distribution[data+1] * codec->length - x;
    }
                
    if (init_base > codec->base) 
        ac_propagate_carry(codec); // overflow = carry

    if (codec->length < AC__MinLength)
        ac_renorm_enc_interval(codec);        // renormalization
}

//----------------------------------------------------------------------------------------------------------------------
uint32_t ac_decode_static(struct arithmetic_codec* codec, struct static_model* model)
{
    assert(codec->mode == 2);  // decoder not initialized

    uint32_t n, s, x, y = codec->length;

    if (model->decoder_table) 
    {
        // use table look-up for faster decoding
        uint32_t dv = codec->value / (codec->length >>= DM__LengthShift);
        uint32_t t = dv >> model->table_shift;

        // initial decision based on table look-up
        s = model->decoder_table[t];
        n = model->decoder_table[t+1] + 1;

        while (n > s + 1) 
        {
            // finish with bisection search
            uint32_t m = (s + n) >> 1;
            if (model->distribution[m] > dv) 
                n = m; 
            else 
                s = m;
        }

        // compute products
        x = model->distribution[s] * codec->length;
        if (s != model->last_symbol) y = model->distribution[s+1] * codec->length;
    }
    else 
    {
        // decode using only multiplications
        x = s = 0;
        codec->length >>= DM__LengthShift;
        uint32_t m = (n = model->data_symbols) >> 1;

        // decode via bisection search
        do 
        {
            uint32_t z = codec->length * model->distribution[m];
            if (z > codec->value) 
            {
                n = m;
                y = z;  // value is smaller
            }
            else 
            {
                s = m;
                x = z; // value is larger or equal
            }
        } while ((m = (s + n) >> 1) != s);
    }

    // update interval
    codec->value -= x;
    codec->length = y - x;

    if (codec->length < AC__MinLength) 
        ac_renorm_dec_interval(codec);        // renormalization

    return s;
}

//----------------------------------------------------------------------------------------------------------------------
uint8_t* ac_get_buffer(struct arithmetic_codec* codec)
{
    return codec->code_buffer;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_terminate(struct arithmetic_codec* codec)
{
    AC_FREE(codec->new_buffer);
}

#endif // __ARITHMETIC_CODEC__IMPLEMENTATION__
