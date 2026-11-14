// Frame codec tests. The shared vectors in protocol/test_vectors/frame_vectors.json are the same
// ones pi/tests/test_uart_protocol.py checks, so both sides agree byte for byte.

#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <protocol.h>

using namespace protocol;

namespace
{
struct Vector
{
    std::string name;
    uint8_t type;
    std::vector<uint8_t> payload;
    std::vector<uint8_t> encoded;
};

std::vector<uint8_t> fromHex(const std::string &hex)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return bytes;
}

// Minimal reader for the flat vector file: finds "key": "value" pairs in order.
std::string stringField(const std::string &object, const std::string &key)
{
    const std::string marker = "\"" + key + "\"";
    size_t at = object.find(marker);
    if (at == std::string::npos) return "";
    at = object.find('"', object.find(':', at) + 1);
    const size_t end = object.find('"', at + 1);
    return object.substr(at + 1, end - at - 1);
}

std::vector<Vector> loadVectors()
{
    std::ifstream file(FRAME_VECTORS_PATH);
    std::stringstream text;
    text << file.rdbuf();
    const std::string json = text.str();

    std::vector<Vector> vectors;
    size_t start = json.find('{');
    while (start != std::string::npos)
    {
        const size_t end = json.find('}', start);
        const std::string object = json.substr(start, end - start + 1);
        Vector v;
        v.name = stringField(object, "name");
        v.type = static_cast<uint8_t>(std::stoul(stringField(object, "message_type"), nullptr, 16));
        v.payload = fromHex(stringField(object, "payload_hex"));
        v.encoded = fromHex(stringField(object, "encoded_hex"));
        vectors.push_back(v);
        start = json.find('{', end);
    }
    return vectors;
}

bool feedAll(FrameParser &parser, const std::vector<uint8_t> &bytes, Frame &frame)
{
    bool got = false;
    for (uint8_t b : bytes) got = parser.feed(b, frame) || got;
    return got;
}

const std::vector<uint8_t> DISPENSE_CMD_FRAME = fromHex("aa5510060102030402030b");
} // namespace

void test_vectors_file_loaded()
{
    TEST_ASSERT_GREATER_OR_EQUAL(2, loadVectors().size());
}

void test_encode_matches_vectors()
{
    for (const Vector &v : loadVectors())
    {
        uint8_t out[MAX_FRAME_LENGTH];
        const uint8_t length = encodeFrame(v.type, v.payload.data(), v.payload.size(), out, sizeof(out));
        TEST_ASSERT_EQUAL_MESSAGE(v.encoded.size(), length, v.name.c_str());
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v.encoded.data(), out, length, v.name.c_str());
    }
}

void test_parse_matches_vectors()
{
    for (const Vector &v : loadVectors())
    {
        FrameParser parser;
        Frame frame;
        TEST_ASSERT_TRUE_MESSAGE(feedAll(parser, v.encoded, frame), v.name.c_str());
        TEST_ASSERT_EQUAL_HEX8(v.type, frame.type);
        TEST_ASSERT_EQUAL(v.payload.size(), frame.length);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(v.payload.data(), frame.payload, frame.length);
    }
}

void test_parser_skips_noise_and_text_before_sync()
{
    std::vector<uint8_t> bytes = {'P', 'L', 'A', 'Y', ':', '1', '\n', 0xAA, 0xAA};
    bytes.insert(bytes.end(), DISPENSE_CMD_FRAME.begin() + 1, DISPENSE_CMD_FRAME.end());
    FrameParser parser;
    Frame frame;
    TEST_ASSERT_TRUE(feedAll(parser, bytes, frame));
    TEST_ASSERT_EQUAL_HEX8(MSG_DISPENSE_CMD, frame.type);
}

void test_parser_rejects_bad_crc_then_recovers()
{
    std::vector<uint8_t> bad = DISPENSE_CMD_FRAME;
    bad.back() ^= 0xFF;
    FrameParser parser;
    Frame frame;
    TEST_ASSERT_FALSE(feedAll(parser, bad, frame));
    TEST_ASSERT_TRUE(feedAll(parser, DISPENSE_CMD_FRAME, frame));
}

void test_parser_rejects_oversized_length()
{
    const std::vector<uint8_t> bytes = {0xAA, 0x55, 0x10, MAX_PAYLOAD_LENGTH + 1};
    FrameParser parser;
    Frame frame;
    TEST_ASSERT_FALSE(feedAll(parser, bytes, frame));
    TEST_ASSERT_TRUE(feedAll(parser, DISPENSE_CMD_FRAME, frame));
}

// Regression: dispense_UI.cpp re-validated the last frame when a byte arrived during a dispense,
// sending a spurious BUSY with the same request ID. The parser must forget a completed frame.
void test_completed_frame_is_not_reported_twice()
{
    FrameParser parser;
    Frame frame;
    TEST_ASSERT_TRUE(feedAll(parser, DISPENSE_CMD_FRAME, frame));
    TEST_ASSERT_FALSE(parser.feed(0x00, frame));
    TEST_ASSERT_FALSE(parser.feed(0x0B, frame));
}

void test_encode_rejects_small_buffer()
{
    const uint8_t payload[6] = {1, 2, 3, 4, 2, 3};
    uint8_t out[10];
    TEST_ASSERT_EQUAL(0, encodeFrame(MSG_DISPENSE_CMD, payload, 6, out, sizeof(out)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_vectors_file_loaded);
    RUN_TEST(test_encode_matches_vectors);
    RUN_TEST(test_parse_matches_vectors);
    RUN_TEST(test_parser_skips_noise_and_text_before_sync);
    RUN_TEST(test_parser_rejects_bad_crc_then_recovers);
    RUN_TEST(test_parser_rejects_oversized_length);
    RUN_TEST(test_completed_frame_is_not_reported_twice);
    RUN_TEST(test_encode_rejects_small_buffer);
    return UNITY_END();
}
