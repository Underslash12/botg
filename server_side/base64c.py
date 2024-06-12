# base64c.py
# a quick base64 encode decode library since nothing was working
# encode is defined on the cpp side, decode on the python side

# x = "SGVsbG8sIHdvcmxkISEhIVVBV2RiNzhhV1Vkal1rajlhd3VkaWhhMHc5ZHVnZ25zZGIgYXdVZHVhaHdkIDg5IDhkeThhd3lkODkqKCooKihXJkQqKCZeQVdkNnVhIDB3ZDlhIHdkICBEKEFXVWQwOSkpKSkp"
decode_table = [0] * 256
encode_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
for i, c in enumerate(encode_table):
    decode_table[ord(c)] = i

# decode an ascii bytes object to a list of integers
def decode(src):
    src_len = len(src)
    src_len_unpadded = src_len - (src_len % 4)
    dst_len = (src_len_unpadded // 4) * 3 + 4
    dst = [0] * dst_len

    src_index = 0
    dst_index = 0
    while src_index < src_len_unpadded:
        dst[dst_index + 0] = (decode_table[src[src_index + 0]] << 2) | (decode_table[src[src_index + 1]] >> 4)
        dst[dst_index + 1] = ((decode_table[src[src_index + 1]] & 0x0f) << 4) | (decode_table[src[src_index + 2]] >> 2)
        dst[dst_index + 2] = ((decode_table[src[src_index + 2]] & 0x03) << 6) | (decode_table[src[src_index + 3]]) 

        src_index += 4
        dst_index += 3

    if src_len % 4 == 2:
        dst[dst_index + 0] = (decode_table[src[src_index + 0]] << 2) | (decode_table[src[src_index + 1]])
        src_index += 2
        dst_index += 1
    elif src_len % 4 == 3:
        dst[dst_index + 0] = (decode_table[src[src_index + 0]] << 2) | (decode_table[src[src_index + 1]] >> 4)
        dst[dst_index + 1] = ((decode_table[src[src_index + 1]] & 0x0f) << 4) | (decode_table[src[src_index + 2]])
        src_index += 3
        dst_index += 2

    print(dst_index)

    return dst[:dst_index]

# decode a str into a bytes object
def decode_from_str_to_bytes(src):
    return bytes(decode(bytes(src, encoding="ASCII")))