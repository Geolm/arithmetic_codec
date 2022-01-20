#include <stdlib.h>
#include <assert.h>
#include "arithmetic_codec.h"



//-- constants --------------------------------------------------------------------------------------------------------------------
#define AC__MinLength (0x01000000U)         // threshold for renormalization
#define AC__MaxLength (0xFFFFFFFFU)        // maximum AC interval length

// Maximum values for general models
#define DM__LengthShift (15)                    // length bits discarded before mult.
#define DM__MaxCount    (1 << DM__LengthShift)  // for adaptive models


//----------------------------------------------------------------------------------------------------------------------
// Adaptive data model
//----------------------------------------------------------------------------------------------------------------------

struct adaptive_data_model
{
    unsigned int *distribution, *symbol_count, *decoder_table;
    unsigned int total_count, update_cycle, symbols_until_update;
    unsigned int data_symbols, last_symbol, table_size, table_shift;
};

void adaptive_data_model_update(struct adaptive_data_model* model, int from_encoder);

//----------------------------------------------------------------------------------------------------------------------
struct adaptive_data_model* adaptive_data_model_init(unsigned int number_of_symbols)
{
    struct adaptive_data_model* model = (struct adaptive_data_model*) malloc(sizeof(struct adaptive_data_model));

    model->data_symbols = 0;
    model->distribution = NULL;

    adaptive_data_model_set_alphabet(model, number_of_symbols);
    
    return model;
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_data_model_terminate(struct adaptive_data_model* model)
{
    free(model->distribution);
    free(model);
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_data_model_reset(struct adaptive_data_model* model)
{
    if (model->data_symbols == 0) 
        return;

    // restore probability estimates to uniform distribution
    model->total_count = 0;
    model->update_cycle = model->data_symbols;

    for (unsigned int k = 0; k < model->data_symbols; k++) 
        model->symbol_count[k] = 1;

    adaptive_data_model_update(model, 0);
    model->symbols_until_update = model->update_cycle = (model->data_symbols + 6) >> 1;
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_data_model_set_alphabet(struct adaptive_data_model* model, unsigned int number_of_symbols)
{
    assert(number_of_symbols>1 && (number_of_symbols <= (1 << 11))); // invalid number of data symbols

    if (model->data_symbols != number_of_symbols) 
    {
        // assign memory for data model
        model->data_symbols = number_of_symbols;
        model->last_symbol = model->data_symbols - 1;
        free(model->distribution);

        // define size of table for fast decoding
        if (model->data_symbols > 16) 
        {
            unsigned int table_bits = 3;
            while (model->data_symbols > (1U << (table_bits + 2))) ++table_bits;
            model->table_size  = 1 << table_bits;
            model->table_shift = DM__LengthShift - table_bits;
            model->distribution = (unsigned int*) malloc(sizeof(unsigned int) * (2 * model->data_symbols+model->table_size+2));
            model->decoder_table = model->distribution + 2 * model->data_symbols;
            assert(model->distribution != NULL);
        }
        else 
        {
            // small alphabet: no table needed
            model->decoder_table = 0;
            model->table_size = model->table_shift = 0;
            model->distribution = (unsigned int*) malloc( sizeof(unsigned int) * 2 * model->data_symbols);
        }
        model->symbol_count = model->distribution + model->data_symbols;
        assert(model->distribution != NULL); // cannot assign model memory
    }

    // initialize model
    adaptive_data_model_reset(model);
}

//----------------------------------------------------------------------------------------------------------------------
void adaptive_data_model_update(struct adaptive_data_model* model, int from_encoder)
{
    if ((model->total_count += model->update_cycle) > DM__MaxCount) 
    {
        model->total_count = 0;
        for (unsigned int n = 0; n < model->data_symbols; n++)
            model->total_count += (model->symbol_count[n] = (model->symbol_count[n] + 1) >> 1);
    }

    // compute cumulative distribution, decoder table
    unsigned int k, sum = 0, s = 0;
    unsigned int scale = 0x80000000U / model->total_count;

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
            unsigned int w = model->distribution[k] >> model->table_shift;
            while (s < w) model->decoder_table[++s] = k - 1;
        }
        model->decoder_table[0] = 0;
        while (s <= model->table_size) model->decoder_table[++s] = model->data_symbols - 1;
    }

    // set frequency of model updates
    model->update_cycle = (5 * model->update_cycle) >> 2;
    unsigned int max_cycle = (model->data_symbols + 6) << 3;
    if (model->update_cycle > max_cycle) 
        model->update_cycle = max_cycle;
    model->symbols_until_update = model->update_cycle;
}

//----------------------------------------------------------------------------------------------------------------------
unsigned int adaptive_data_model_get_symbol_count(const struct adaptive_data_model* model, unsigned int symbol)
{
    assert(symbol < model->data_symbols); // invalid data symbols
    assert(model->distribution != NULL); // adaptive model should be initialized

    return model->symbol_count[symbol];
}

//----------------------------------------------------------------------------------------------------------------------
// Static data model
//----------------------------------------------------------------------------------------------------------------------

struct static_data_model
{
    unsigned int *distribution, *decoder_table;
    unsigned int data_symbols, last_symbol, table_size, table_shift;
};

//----------------------------------------------------------------------------------------------------------------------
struct static_data_model* static_data_model_init(unsigned int number_of_symbols, const float *probability)
{
    struct static_data_model* model = (struct static_data_model*) malloc(sizeof(struct static_data_model));

    model->data_symbols = 0;
    model->distribution = NULL;

    static_data_model_set_distribution(model, number_of_symbols, probability);

    return model;
}

//----------------------------------------------------------------------------------------------------------------------
void static_data_model_set_distribution(struct static_data_model* model, unsigned int number_of_symbols, const float *probability)
{
    assert(number_of_symbols>1 && (number_of_symbols <= (1 << 11))); // invalid number of data symbols

    if (model->data_symbols != number_of_symbols) 
    {
        // assign memory for data model
        model->data_symbols = number_of_symbols;
        model->last_symbol = model->data_symbols - 1;
        free(model->distribution);

        // define size of table for fast decoding
        if (model->data_symbols > 16) 
        {
            unsigned int table_bits = 3;
            while (model->data_symbols > (1U << (table_bits + 2))) 
                ++table_bits;
            model->table_size  = 1 << table_bits;
            model->table_shift = DM__LengthShift - table_bits;
            model->distribution = (unsigned int*) malloc(sizeof(unsigned int) * (model->data_symbols+model->table_size+2));
            model->decoder_table = model->distribution + model->data_symbols;
        }
        else 
        {                                  // small alphabet: no table needed
            model->decoder_table = 0;
            model->table_size = model->table_shift = 0;
            model->distribution = (unsigned int*) malloc(sizeof(unsigned int) * model->data_symbols);
        }
        assert(model->distribution != NULL);
    }
                                // compute cumulative distribution, decoder table
    unsigned int s = 0;
    float sum = 0.0f, p = 1.0f / (float)(model->data_symbols);

    for (unsigned k = 0; k < model->data_symbols; k++) 
    {
        if (probability) 
            p = probability[k];

        assert(p>=0.f && p<= 1.f);
        
        model->distribution[k] = (unsigned int)(sum * (1 << DM__LengthShift));
        sum += p;

        if (model->table_size == 0) 
            continue;

        unsigned int w = model->distribution[k] >> model->table_shift;
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
void static_data_model_terminate(struct static_data_model* model)
{
    free(model->distribution);
    free(model);
}

//----------------------------------------------------------------------------------------------------------------------
// Arithmetic Codec
//----------------------------------------------------------------------------------------------------------------------

struct arithmetic_codec
{
    unsigned char *code_buffer, *new_buffer, *ac_pointer;
    unsigned int base, value, length;                     // arithmetic coding state
    unsigned int buffer_size;
    unsigned int mode;     // mode: 0 = undef, 1 = encoder, 2 = decoder
};

//----------------------------------------------------------------------------------------------------------------------
inline static void ac_propagate_carry(struct arithmetic_codec* codec)
{
    unsigned char * p;            
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
        *codec->ac_pointer++ = (unsigned char)(codec->base >> 24);
        codec->base <<= 8;
    } while ((codec->length <<= 8) < AC__MinLength);        // length multiplied by 256
}

//----------------------------------------------------------------------------------------------------------------------
inline static void ac_renorm_dec_interval(struct arithmetic_codec* codec)
{
    do // read least-significant byte
    {
        codec->value = (codec->value << 8) | (unsigned int)(*++codec->ac_pointer);
    } while ((codec->length <<= 8) < AC__MinLength);        // length multiplied by 256
}

//----------------------------------------------------------------------------------------------------------------------
struct arithmetic_codec* ac_init(void)
{
    struct arithmetic_codec* codec = (struct arithmetic_codec*) malloc(sizeof(struct arithmetic_codec));

    codec->mode = codec->buffer_size = 0;
    codec->new_buffer = codec->code_buffer = NULL;

    return codec;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_set_buffer(struct arithmetic_codec* codec, unsigned int max_code_bytes, unsigned char *user_buffer)
{
    assert(codec->mode == 0); // cannot set buffer while encoding or decoding

    if (user_buffer != NULL) 
    {
        // user provides memory buffer
        codec->buffer_size = max_code_bytes;
        codec->code_buffer = user_buffer; // set buffer for compressed data
        free(codec->new_buffer); // free anything previously assigned              
        codec->new_buffer = NULL;
        return;
    }

    // enough space available in the current buffer
    if (max_code_bytes <= codec->buffer_size) 
        return;

    codec->buffer_size = max_code_bytes; // assign new memory
    free(codec->new_buffer);    // free anything previously assigned
    codec->new_buffer = (unsigned char*) malloc(codec->buffer_size + 16); // 16 extra bytes
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
    codec->value = ((unsigned int)(codec->code_buffer[0]) << 24) |
                   ((unsigned int)(codec->code_buffer[1]) << 16) |
                   ((unsigned int)(codec->code_buffer[2]) <<  8) |
                    (unsigned int)(codec->code_buffer[3]);
}

//----------------------------------------------------------------------------------------------------------------------
unsigned int ac_stop_encoder(struct arithmetic_codec* codec)
{
    assert(codec->mode == 1); // invalid to stop encoder
    codec->mode = 0;

    unsigned int init_base = codec->base;            // done encoding: set final data bytes

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

    unsigned int code_bytes = (unsigned int)(codec->ac_pointer - codec->code_buffer);
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
void ac_put_bit(struct arithmetic_codec* codec, unsigned int bit)
{
    assert(codec->mode == 1);  // encoder not initialized

    codec->length >>= 1;    // halve interval
    if (bit) 
    {
        unsigned int init_base = codec->base;
        codec->base += codec->length;   // move base
        if (init_base > codec->base) 
            ac_propagate_carry(codec);  // overflow = carry
    }

    if (codec->length < AC__MinLength) 
        ac_renorm_enc_interval(codec); // renormalization
}

//----------------------------------------------------------------------------------------------------------------------
unsigned int ac_get_bit(struct arithmetic_codec* codec)
{
    assert(codec->mode == 2); //  decoder not initialized   

    codec->length >>= 1;  // halve interval
    unsigned int bit = (codec->value >= codec->length);  // decode bit
    if (bit) 
        codec->value -= codec->length; // move base

    if (codec->length < AC__MinLength) 
        ac_renorm_dec_interval(codec);  // renormalization

    return bit;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_put_bits(struct arithmetic_codec* codec, unsigned int data, unsigned int number_of_bits)
{
    assert(codec->mode == 1);  // encoder not initialized
    assert((number_of_bits > 0) && (number_of_bits < 21));  // invalid number of bits
    assert(data < (1U << number_of_bits)); // invalid data

    unsigned int init_base = codec->base;
    codec->base += data * (codec->length >>= number_of_bits);            // new interval base and length

    if (init_base > codec->base) 
        ac_propagate_carry(codec);                 // overflow = carry

    if (codec->length < AC__MinLength) 
        ac_renorm_enc_interval(codec);        // renormalization
}

//----------------------------------------------------------------------------------------------------------------------
unsigned int ac_get_bits(struct arithmetic_codec* codec, unsigned int number_of_bits)
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
void ac_encode_adaptive(struct arithmetic_codec* codec, unsigned int data, struct adaptive_data_model* model)
{
    assert(codec->mode == 1);  // encoder not initialized
    assert(data < model->data_symbols); // invalid data symbols
    assert(model->distribution != NULL); // adaptive model should be initialized
    
    unsigned int x;
    unsigned int init_base = codec->base;

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
        adaptive_data_model_update(model, 1);
        

}

//----------------------------------------------------------------------------------------------------------------------
unsigned int ac_decode_adaptive(struct arithmetic_codec* codec, struct adaptive_data_model* model)
{
    assert(codec->mode == 2); // decoder not initialized
    assert(model->distribution != NULL); // adaptive model should be initialized

    unsigned int n, s, x, y = codec->length;

    if (model->decoder_table) 
    {
        // use table look-up for faster decoding
        unsigned int dv = codec->value / (codec->length >>= DM__LengthShift);
        unsigned int t = dv >> model->table_shift;

        s = model->decoder_table[t];         // initial decision based on table look-up
        n = model->decoder_table[t+1] + 1;

        while (n > s + 1) 
        {                        // finish with bisection search
            unsigned int m = (s + n) >> 1;
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
        unsigned int m = (n = model->data_symbols) >> 1;

        // decode via bisection search
        do 
        {
            unsigned int z = codec->length * model->distribution[m];
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
        adaptive_data_model_update(model, 0);

    return s;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_encode_static(struct arithmetic_codec* codec, unsigned int data, struct static_data_model* model)
{
    assert(codec->mode == 1);   // encoder not initialized
    assert(data < model->data_symbols); // invalid data symbol

    unsigned int x, init_base = codec->base;

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
unsigned int ac_decode_static(struct arithmetic_codec* codec, struct static_data_model* model)
{
    assert(codec->mode == 2);  // decoder not initialized

    unsigned int n, s, x, y = codec->length;

    if (model->decoder_table) 
    {
        // use table look-up for faster decoding
        unsigned int dv = codec->value / (codec->length >>= DM__LengthShift);
        unsigned int t = dv >> model->table_shift;

        // initial decision based on table look-up
        s = model->decoder_table[t];
        n = model->decoder_table[t+1] + 1;

        while (n > s + 1) 
        {
            // finish with bisection search
            unsigned int m = (s + n) >> 1;
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
        unsigned int m = (n = model->data_symbols) >> 1;

        // decode via bisection search
        do 
        {
            unsigned int z = codec->length * model->distribution[m];
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
unsigned char* ac_get_buffer(struct arithmetic_codec* codec)
{
    return codec->code_buffer;
}

//----------------------------------------------------------------------------------------------------------------------
void ac_terminate(struct arithmetic_codec* codec)
{
    free(codec->new_buffer);
}
