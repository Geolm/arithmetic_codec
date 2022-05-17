#ifndef __ARITHMETIC_CODEC__
#define __ARITHMETIC_CODEC__

// Ported in C from on FastAC by Amir Said's
// https://github.com/richgel999/FastAC

#ifdef __cplusplus
extern "C" {
#endif

struct adaptive_data_model;
struct static_data_model;
struct arithmetic_codec;


//----------------------------------------------------------------------------------------------------------------------
// Adaptive data model
//----------------------------------------------------------------------------------------------------------------------

/*
    Initialized the adaptive data model, returns a pointer to the model

        number_of_symbols   Number of symbols maximum
*/
struct adaptive_data_model* adaptive_data_model_init(unsigned int number_of_symbols);

/*
    Releases memory
*/
void adaptive_data_model_terminate(struct adaptive_data_model* model);

/*
    Reset the statistics of the model (all symbols counter resetted to one)
*/
void adaptive_data_model_reset(struct adaptive_data_model* model);

/*
    Change the number of symbols of the model. It will reset the model
*/
void adaptive_data_model_set_alphabet(struct adaptive_data_model* model, unsigned int number_of_symbols);

/*
    Returns how many time the symbol has been encoded
*/
unsigned int adaptive_data_model_get_symbol_count(const struct adaptive_data_model* model, unsigned int symbol);


//----------------------------------------------------------------------------------------------------------------------
// Static data model
//----------------------------------------------------------------------------------------------------------------------

/*
    Initialized the static data model, returns a pointer to the model

        number_of_symbols   Number of symbols maximum

        probability         Pointer to an array of float containing probability. Array must have the size of number_of_symbols
                            Each float must be [0;1]
*/
struct static_data_model* static_data_model_init(unsigned int number_of_symbols, const float *probability);

/*
    Set up the distribution

        number_of_symbols   Number of symbols maximum

        probability         Pointer to an array of float containing probability. Array must have the size of number_of_symbols
                            Each float must be [0;1]
*/
void static_data_model_set_distribution(struct static_data_model* model, unsigned int number_of_symbols, const float *probability);

/*
    Releases memory
*/
void static_data_model_terminate(struct static_data_model* model);

//----------------------------------------------------------------------------------------------------------------------
// Arithmetic Codec
//----------------------------------------------------------------------------------------------------------------------

/*
    Initialized the arithmetic codec, returns a pointer to the codec

    You need to call ac_set_buffer() before starting encode/decode
*/
struct arithmetic_codec* ac_init(void);

/*
    Set the buffer for compressed data

        max_code_bytes  Maximum size of the buffer in bytes

        user_buffer     If the pointer to the buffer is NULL, memory will be allocated internally using malloc()
*/
void ac_set_buffer(struct arithmetic_codec* codec, unsigned int max_code_bytes, unsigned char *user_buffer);

/*
    Set the codec to encoding mode
*/
void ac_start_encoder(struct arithmetic_codec* codec);

/*
    Set the codec to decoding mode
*/
void ac_start_decoder(struct arithmetic_codec* codec);

/*
    Stop encoding, return the number of bytes used in the compressed buffer
*/
unsigned int ac_stop_encoder(struct arithmetic_codec* codec);

/*
    Stop the decoder
*/
void ac_stop_decoder(struct arithmetic_codec* codec);

/*
    Store one bit in the buffer
*/
void ac_put_bit(struct arithmetic_codec* codec, unsigned int bit);

/*
    Get one bit from the buffer, returns the bit
*/
unsigned int ac_get_bit(struct arithmetic_codec* codec);

/*
    Store multiple bits of data in the buffer
*/
void ac_put_bits(struct arithmetic_codec* codec, unsigned int data, unsigned int number_of_bits);

/*
    Get multiple bits of data from the buffer, returns the bits
*/
unsigned int ac_get_bits(struct arithmetic_codec* codec, unsigned int number_of_bits);

/*
    Encode data using an adaptive model, the model should be initialized
*/
void ac_encode_adaptive(struct arithmetic_codec* codec, unsigned int data, struct adaptive_data_model* model);

/*
    Decode the next data from the buffer using an adaptative model
*/
unsigned int ac_decode_adaptive(struct arithmetic_codec* codec, struct adaptive_data_model* model);

/*
    Encode data using an static model, the model should be initialized
*/
void ac_encode_static(struct arithmetic_codec* codec, unsigned int data, struct static_data_model* model);

/*
    Decode the next data from the buffer using an static model
*/
unsigned int ac_decode_static(struct arithmetic_codec* codec, struct static_data_model* model);

/*
    Returns a pointer to the compressed buffer
*/
unsigned char* ac_get_buffer(struct arithmetic_codec* codec);

/*
    Returns a pointer to the compressed buffer
*/
void ac_terminate(struct arithmetic_codec* codec);

#ifdef __cplusplus
}
#endif



#endif // __ARITHMETIC_CODEC__
