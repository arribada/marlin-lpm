#include <array>
#include <cstdint>

#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include "simple.pb.h"
#include <pb_decode.h>
#include <pb_encode.h>

#define CONFIG_SAMPLE_BUFFER_SIZE 8

constexpr int lucky_number = 13;

bool encode_message(uint8_t *buffer, size_t buffer_size, size_t *message_length)
{
    SimpleMessage message = SimpleMessage_init_zero;
    pb_ostream_t  stream  = pb_ostream_from_buffer(buffer, buffer_size);
    message.lucky_number  = lucky_number;
    for (int i = 0; i < CONFIG_SAMPLE_BUFFER_SIZE; ++i)
    {
        message.buffer[i] = (uint8_t)(i * 2); // NOLINT
    }

    if (!pb_encode(&stream, SimpleMessage_fields, &message))
    {
        printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return false;
    }

    *message_length = stream.bytes_written;
    return true;
}

SimpleMessage decode_message(uint8_t *buffer, size_t message_length)
{
    SimpleMessage message = SimpleMessage_init_zero;

    pb_istream_t stream = pb_istream_from_buffer(buffer, message_length);

    bool status = pb_decode(&stream, SimpleMessage_fields, &message);
    zassert_true(status);

    return message;
}

ZTEST(nanopb_demo, test_demo)
{
    std::array<uint8_t, SimpleMessage_size> buffer{};
    size_t                                  message_length = 0;
    zassert_true(encode_message(buffer.data(), buffer.size(), &message_length));
    auto message = decode_message(buffer.data(), message_length);
    zassert_equal(message.lucky_number, lucky_number);
    zassert_equal(message.buffer[0], 0);
}

ZTEST_SUITE(nanopb_demo, NULL, NULL, NULL, NULL, NULL);
