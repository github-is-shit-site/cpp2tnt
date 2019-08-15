#include "base64.h"
#include "msgpuck/msgpuck.h"
#include "mp_writer.h"
#include "connection.h"
#include "proto.h"

extern "C"
{
    #include "sha1.h"
}

using namespace std;

static void scramble_prepare(void *out, const void *salt, string_view pass) noexcept
{
    unsigned char hash1[tnt::SCRAMBLE_SIZE];
    unsigned char hash2[tnt::SCRAMBLE_SIZE];
    SHA1_CTX ctx;

    SHA1Init(&ctx);
    SHA1Update(&ctx, reinterpret_cast<const unsigned char*>(pass.data()), static_cast<uint32_t>(pass.size()));
    SHA1Final(hash1, &ctx);

    SHA1Init(&ctx);
    SHA1Update(&ctx, hash1, tnt::SCRAMBLE_SIZE);
    SHA1Final(hash2, &ctx);

    SHA1Init(&ctx);
    SHA1Update(&ctx, reinterpret_cast<const unsigned char*>(salt), tnt::SCRAMBLE_SIZE);
    SHA1Update(&ctx, hash2, tnt::SCRAMBLE_SIZE);
    SHA1Final(hash2, &ctx);

    uint8_t *dst = reinterpret_cast<uint8_t*>(out);
    for (int i = 0; i < tnt::SCRAMBLE_SIZE; ++i)
        dst[i] = hash1[i] ^ hash2[i];
}

mp_writer::mp_writer(tnt::connection &cn) : mp_writer(cn, cn.output_buffer()) {}

mp_writer::mp_writer(tnt::connection &cn, wtf_buffer &buf)
    : _cn(cn), _buf(buf)
{

}

void mp_writer::encode_header(tnt::request_type req_type)
{
    // close previous request and its opened containers
    while (!_opened_containers.empty()) end();

    // ensure we have free space
    if (_buf.capacity() - _buf.size() < 1024)
        _buf.reserve(static_cast<size_t>(_buf.capacity() * 1.5));

    size_t head_offset = _buf.size();
    _opened_containers.push({head_offset, std::numeric_limits<uint32_t>::max()});
    mp_store_u8(_buf.end, 0xce); // 0xce -> unit32 (place now to distinguish request header and containers)
    _buf.end += 5;

    _buf.end = mp_encode_map(_buf.end, 2);
    _buf.end = mp_encode_uint(mp_encode_uint(_buf.end, tnt::header_field::CODE), static_cast<uint8_t>(req_type));
    _buf.end = mp_encode_uint(mp_encode_uint(_buf.end, tnt::header_field::SYNC), _cn.next_request_id());
}

void mp_writer::encode_auth_request()
{
    auto &cs = _cn.connection_string_parts();
    encode_auth_request(cs.user, cs.password);
}

void mp_writer::encode_auth_request(std::string_view user, std::string_view password)
{
    encode_header(tnt::request_type::AUTH);

    _buf.end = mp_encode_map(_buf.end, 2);
    _buf.end = mp_encode_strl(mp_encode_uint(_buf.end, tnt::body_field::USER_NAME),
                             static_cast<uint32_t>(user.size()));
    memcpy(_buf.end, user.data(), user.size());
    _buf.end += user.size();

    _buf.end = mp_encode_uint(_buf.end, tnt::body_field::TUPLE);
    string_view b64_salt = {
        _cn.greeting().data() + tnt::VERSION_SIZE,
        tnt::SCRAMBLE_SIZE + tnt::SALT_SIZE
    };
    char salt[64];
    _buf.end = mp_encode_array(_buf.end, 2);
    _buf.end = mp_encode_str(_buf.end, "chap-sha1", 9);
    _buf.end = mp_encode_strl(_buf.end, tnt::SCRAMBLE_SIZE);
    base64_decode(b64_salt.data(), tnt::SALT_SIZE, salt, 64);
    scramble_prepare(_buf.end, salt, password);
    _buf.end += tnt::SCRAMBLE_SIZE;

    end();
}

void mp_writer::begin_call(string_view fn_name)
{
    encode_header(tnt::request_type::CALL);

    _buf.end = mp_encode_map(_buf.end, 2);
    _buf.end = mp_encode_uint(_buf.end, tnt::body_field::FUNCTION_NAME);
    _buf.end = mp_encode_str(_buf.end, fn_name.data(), static_cast<uint32_t>(fn_name.size()));
    _buf.end = mp_encode_uint(_buf.end, tnt::body_field::TUPLE);
    // a caller must append an array of arguments (zero-length one if void)
}

void mp_writer::begin_array(uint32_t max_size)
{
    if (max_size)
        _opened_containers.push({_buf.size(), max_size});
    _buf.end = mp_encode_array(_buf.end, max_size);
}

void mp_writer::begin_map(uint32_t max_size)
{
    if (max_size)
        _opened_containers.push({_buf.size(), max_size * 2});
    _buf.end = mp_encode_map(_buf.end, max_size);
}

void mp_writer::end()
{
    if (_opened_containers.empty())
        throw range_error("no container to finalize");
    auto &c = _opened_containers.pop();
    char *head = _buf.data() + c.head_offset;

    if (static_cast<uint8_t>(*head) == 0xce)  // request head
    {
        size_t size = static_cast<size_t>(_buf.end - head);
        if (!size)
            return;

        if (size > c.max_cardinality)
            throw range_error("request size exceeded");
        mp_store_u32(++head, static_cast<uint32_t>(size - 5));
        return;
    }

    // mp_encode_array() may reduce header's size if actual cardinality
    // is smaller than initial value, so we update the header directly.

    uint32_t num_bytes = 0;
    uint32_t actual_cardinality = c.items_count;
    auto container_type = mp_typeof(*head);
    if (container_type == MP_ARRAY)
    {
        // get current header size
        num_bytes = mp_sizeof_array(c.max_cardinality);
        if (num_bytes == 1)
        {
            // replace 1-byte header with new size
            mp_encode_array(head, actual_cardinality);
            return;
        }
    }
    else if (container_type == MP_ARRAY)
    {
        // map cardinality
        actual_cardinality = c.items_count / 2;
        // get current header size
        num_bytes = mp_sizeof_map(c.max_cardinality);
        if (num_bytes == 1)
        {
            // replace 1-byte header with new size
            mp_encode_map(head, actual_cardinality);
            return;
        }
    }
    else
    {
        throw runtime_error("unexpected container header");
    }

    switch (num_bytes)
    {
    case 3:
        mp_store_u16(++head, static_cast<uint16_t>(actual_cardinality));
        break;
    case 5:
        mp_store_u32(++head, actual_cardinality);
        break;
    default:
        throw runtime_error("wtf?");
    }
}

