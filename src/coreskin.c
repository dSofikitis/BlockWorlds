#include "coreskin.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t byte_pos;
    uint32_t bit_buf;
    uint32_t bit_cnt;
} cs_bitreader_t;

typedef struct {
    uint16_t counts[16];
    uint16_t symbols[288];
} cs_huff_t;

static int cs_br_init(cs_bitreader_t *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->byte_pos = 0;
    br->bit_buf = 0;
    br->bit_cnt = 0;
    return 0;
}

static int cs_br_getbit(cs_bitreader_t *br, uint32_t *out) {
    if (br->bit_cnt == 0) {
        if (br->byte_pos >= br->size) {
            return -1;
        }
        br->bit_buf = br->data[br->byte_pos];
        br->byte_pos++;
        br->bit_cnt = 8;
    }
    *out = br->bit_buf & 1u;
    br->bit_buf >>= 1;
    br->bit_cnt--;
    return 0;
}

static int cs_br_getbits(cs_bitreader_t *br, uint32_t n, uint32_t *out) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bit = 0;
        if (cs_br_getbit(br, &bit) != 0) {
            return -1;
        }
        value |= bit << i;
    }
    *out = value;
    return 0;
}

static void cs_br_align(cs_bitreader_t *br) {
    br->bit_buf = 0;
    br->bit_cnt = 0;
}

static int cs_huff_build(cs_huff_t *h, const uint8_t *lengths, uint32_t count) {
    uint16_t offsets[16];
    for (uint32_t i = 0; i < 16; i++) {
        h->counts[i] = 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        h->counts[lengths[i]]++;
    }
    h->counts[0] = 0;
    uint32_t total = 0;
    uint32_t max_code = 0;
    for (uint32_t i = 1; i < 16; i++) {
        total += h->counts[i];
        max_code += (uint32_t)h->counts[i] << (16 - i);
    }
    if (max_code > (1u << 16)) {
        return -1;
    }
    offsets[0] = 0;
    offsets[1] = 0;
    for (uint32_t i = 1; i < 15; i++) {
        offsets[i + 1] = (uint16_t)(offsets[i] + h->counts[i]);
    }
    for (uint32_t i = 0; i < count; i++) {
        if (lengths[i] != 0) {
            h->symbols[offsets[lengths[i]]] = (uint16_t)i;
            offsets[lengths[i]]++;
        }
    }
    (void)total;
    return 0;
}

static int cs_huff_decode(cs_bitreader_t *br, const cs_huff_t *h, uint32_t *out) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (uint32_t len = 1; len < 16; len++) {
        uint32_t bit = 0;
        if (cs_br_getbit(br, &bit) != 0) {
            return -1;
        }
        code |= (int)bit;
        int cnt = (int)h->counts[len];
        if (code - first < cnt) {
            *out = h->symbols[index + (code - first)];
            return 0;
        }
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t cs_len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t cs_len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t cs_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t cs_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int cs_inflate_block_data(cs_bitreader_t *br, const cs_huff_t *lit,
                                 const cs_huff_t *dist, uint8_t *out,
                                 size_t out_cap, size_t *out_pos) {
    for (;;) {
        uint32_t sym = 0;
        if (cs_huff_decode(br, lit, &sym) != 0) {
            return -1;
        }
        if (sym == 256) {
            return 0;
        }
        if (sym < 256) {
            if (*out_pos >= out_cap) {
                return -1;
            }
            out[*out_pos] = (uint8_t)sym;
            (*out_pos)++;
        } else {
            if (sym < 257 || sym > 285) {
                return -1;
            }
            uint32_t li = sym - 257;
            uint32_t extra = 0;
            if (cs_br_getbits(br, cs_len_extra[li], &extra) != 0) {
                return -1;
            }
            uint32_t length = (uint32_t)cs_len_base[li] + extra;
            uint32_t dsym = 0;
            if (cs_huff_decode(br, dist, &dsym) != 0) {
                return -1;
            }
            if (dsym > 29) {
                return -1;
            }
            uint32_t dextra = 0;
            if (cs_br_getbits(br, cs_dist_extra[dsym], &dextra) != 0) {
                return -1;
            }
            uint32_t distance = (uint32_t)cs_dist_base[dsym] + dextra;
            if (distance > *out_pos) {
                return -1;
            }
            if (*out_pos + length > out_cap) {
                return -1;
            }
            size_t src = *out_pos - distance;
            for (uint32_t k = 0; k < length; k++) {
                out[*out_pos] = out[src];
                (*out_pos)++;
                src++;
            }
        }
    }
}

static void cs_build_fixed(cs_huff_t *lit, cs_huff_t *dist) {
    uint8_t lit_lengths[288];
    uint8_t dist_lengths[30];
    uint32_t i;
    for (i = 0; i < 144; i++) {
        lit_lengths[i] = 8;
    }
    for (i = 144; i < 256; i++) {
        lit_lengths[i] = 9;
    }
    for (i = 256; i < 280; i++) {
        lit_lengths[i] = 7;
    }
    for (i = 280; i < 288; i++) {
        lit_lengths[i] = 8;
    }
    for (i = 0; i < 30; i++) {
        dist_lengths[i] = 5;
    }
    cs_huff_build(lit, lit_lengths, 288);
    cs_huff_build(dist, dist_lengths, 30);
}

static const uint8_t cs_clcl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static int cs_inflate_dynamic(cs_bitreader_t *br, uint8_t *out, size_t out_cap,
                              size_t *out_pos) {
    uint32_t hlit = 0;
    uint32_t hdist = 0;
    uint32_t hclen = 0;
    if (cs_br_getbits(br, 5, &hlit) != 0) {
        return -1;
    }
    if (cs_br_getbits(br, 5, &hdist) != 0) {
        return -1;
    }
    if (cs_br_getbits(br, 4, &hclen) != 0) {
        return -1;
    }
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 30) {
        return -1;
    }
    uint8_t cl_lengths[19];
    for (uint32_t i = 0; i < 19; i++) {
        cl_lengths[i] = 0;
    }
    for (uint32_t i = 0; i < hclen; i++) {
        uint32_t v = 0;
        if (cs_br_getbits(br, 3, &v) != 0) {
            return -1;
        }
        cl_lengths[cs_clcl_order[i]] = (uint8_t)v;
    }
    cs_huff_t cl_huff;
    if (cs_huff_build(&cl_huff, cl_lengths, 19) != 0) {
        return -1;
    }
    uint8_t lengths[286 + 30];
    uint32_t total = hlit + hdist;
    uint32_t n = 0;
    while (n < total) {
        uint32_t sym = 0;
        if (cs_huff_decode(br, &cl_huff, &sym) != 0) {
            return -1;
        }
        if (sym < 16) {
            lengths[n] = (uint8_t)sym;
            n++;
        } else if (sym == 16) {
            if (n == 0) {
                return -1;
            }
            uint32_t rep = 0;
            if (cs_br_getbits(br, 2, &rep) != 0) {
                return -1;
            }
            rep += 3;
            uint8_t prev = lengths[n - 1];
            if (n + rep > total) {
                return -1;
            }
            for (uint32_t k = 0; k < rep; k++) {
                lengths[n] = prev;
                n++;
            }
        } else if (sym == 17) {
            uint32_t rep = 0;
            if (cs_br_getbits(br, 3, &rep) != 0) {
                return -1;
            }
            rep += 3;
            if (n + rep > total) {
                return -1;
            }
            for (uint32_t k = 0; k < rep; k++) {
                lengths[n] = 0;
                n++;
            }
        } else if (sym == 18) {
            uint32_t rep = 0;
            if (cs_br_getbits(br, 7, &rep) != 0) {
                return -1;
            }
            rep += 11;
            if (n + rep > total) {
                return -1;
            }
            for (uint32_t k = 0; k < rep; k++) {
                lengths[n] = 0;
                n++;
            }
        } else {
            return -1;
        }
    }
    cs_huff_t lit_huff;
    cs_huff_t dist_huff;
    if (cs_huff_build(&lit_huff, lengths, hlit) != 0) {
        return -1;
    }
    if (cs_huff_build(&dist_huff, lengths + hlit, hdist) != 0) {
        return -1;
    }
    return cs_inflate_block_data(br, &lit_huff, &dist_huff, out, out_cap, out_pos);
}

static int cs_inflate(const uint8_t *data, size_t size, uint8_t *out,
                      size_t out_cap, size_t *out_len) {
    cs_bitreader_t br;
    cs_br_init(&br, data, size);
    size_t out_pos = 0;
    uint32_t bfinal = 0;
    do {
        uint32_t btype = 0;
        if (cs_br_getbit(&br, &bfinal) != 0) {
            return -1;
        }
        if (cs_br_getbits(&br, 2, &btype) != 0) {
            return -1;
        }
        if (btype == 0) {
            cs_br_align(&br);
            if (br.byte_pos + 4 > br.size) {
                return -1;
            }
            uint32_t len = (uint32_t)br.data[br.byte_pos] |
                           ((uint32_t)br.data[br.byte_pos + 1] << 8);
            uint32_t nlen = (uint32_t)br.data[br.byte_pos + 2] |
                            ((uint32_t)br.data[br.byte_pos + 3] << 8);
            br.byte_pos += 4;
            if ((len ^ 0xFFFFu) != nlen) {
                return -1;
            }
            if (br.byte_pos + len > br.size) {
                return -1;
            }
            if (out_pos + len > out_cap) {
                return -1;
            }
            memcpy(out + out_pos, br.data + br.byte_pos, len);
            br.byte_pos += len;
            out_pos += len;
        } else if (btype == 1) {
            cs_huff_t lit;
            cs_huff_t dist;
            cs_build_fixed(&lit, &dist);
            if (cs_inflate_block_data(&br, &lit, &dist, out, out_cap, &out_pos) != 0) {
                return -1;
            }
        } else if (btype == 2) {
            if (cs_inflate_dynamic(&br, out, out_cap, &out_pos) != 0) {
                return -1;
            }
        } else {
            return -1;
        }
    } while (bfinal == 0);
    *out_len = out_pos;
    return 0;
}

static int cs_zlib_inflate(const uint8_t *data, size_t size, uint8_t *out,
                           size_t out_cap, size_t *out_len) {
    if (size < 2) {
        return -1;
    }
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    uint8_t cm = cmf & 0x0F;
    if (cm != 8) {
        return -1;
    }
    if (((uint32_t)cmf * 256u + (uint32_t)flg) % 31u != 0) {
        return -1;
    }
    uint8_t fdict = (flg >> 5) & 1u;
    if (fdict) {
        return -1;
    }
    return cs_inflate(data + 2, size - 2, out, out_cap, out_len);
}

static uint8_t cs_paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p - a;
    int pb = p - b;
    int pc = p - c;
    if (pa < 0) {
        pa = -pa;
    }
    if (pb < 0) {
        pb = -pb;
    }
    if (pc < 0) {
        pc = -pc;
    }
    if (pa <= pb && pa <= pc) {
        return (uint8_t)a;
    }
    if (pb <= pc) {
        return (uint8_t)b;
    }
    return (uint8_t)c;
}

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t interlace;
    uint8_t has_plte;
    uint32_t plte_count;
    uint8_t plte[256][3];
    uint8_t has_trns;
    uint32_t trns_count;
    uint8_t trns[256];
    uint16_t trns_gray;
    uint16_t trns_r;
    uint16_t trns_g;
    uint16_t trns_b;
} cs_png_info_t;

static uint32_t cs_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t cs_channels(uint8_t color_type) {
    switch (color_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

static int cs_unfilter(uint8_t *raw, uint32_t height, uint32_t stride, uint32_t bpp) {
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = raw + (size_t)y * (1 + stride);
        uint8_t filter = row[0];
        uint8_t *cur = row + 1;
        const uint8_t *prev = (y == 0) ? NULL : (raw + (size_t)(y - 1) * (1 + stride) + 1);
        for (uint32_t i = 0; i < stride; i++) {
            int a = (i >= bpp) ? cur[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
            int x = cur[i];
            switch (filter) {
                case 0: break;
                case 1: x = x + a; break;
                case 2: x = x + b; break;
                case 3: x = x + ((a + b) >> 1); break;
                case 4: x = x + cs_paeth(a, b, c); break;
                default: return -1;
            }
            cur[i] = (uint8_t)x;
        }
    }
    return 0;
}

static uint32_t cs_get_sample(const uint8_t *row, uint32_t index, uint8_t bit_depth) {
    if (bit_depth == 8) {
        return row[index];
    }
    if (bit_depth == 16) {
        return row[index * 2];
    }
    uint32_t bits_per = bit_depth;
    uint32_t per_byte = 8u / bits_per;
    uint32_t byte_index = index / per_byte;
    uint32_t shift = (per_byte - 1u - (index % per_byte)) * bits_per;
    uint32_t mask = (1u << bits_per) - 1u;
    return (row[byte_index] >> shift) & mask;
}

static uint8_t cs_scale_sample(uint32_t v, uint8_t bit_depth) {
    switch (bit_depth) {
        case 1: return v ? 255u : 0u;
        case 2: return (uint8_t)(v * 85u);
        case 4: return (uint8_t)(v * 17u);
        case 8: return (uint8_t)v;
        case 16: return (uint8_t)v;
        default: return 0;
    }
}

static uint8_t *cs_expand(const uint8_t *raw, const cs_png_info_t *info,
                          uint32_t stride) {
    uint32_t w = info->width;
    uint32_t h = info->height;
    uint8_t depth = info->bit_depth;
    uint8_t ct = info->color_type;
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) {
        return NULL;
    }
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = raw + (size_t)y * (1 + stride) + 1;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 255;
            if (ct == 0) {
                uint32_t s = cs_get_sample(row, x, depth);
                uint8_t gray = cs_scale_sample(s, depth);
                r = g = b = gray;
                if (info->has_trns) {
                    uint32_t raw_s = (depth == 16) ?
                        (((uint32_t)row[x * 2] << 8) | row[x * 2 + 1]) : s;
                    if (raw_s == info->trns_gray) {
                        a = 0;
                    }
                }
            } else if (ct == 2) {
                if (depth == 8) {
                    r = row[x * 3 + 0];
                    g = row[x * 3 + 1];
                    b = row[x * 3 + 2];
                    if (info->has_trns && r == info->trns_r &&
                        g == info->trns_g && b == info->trns_b) {
                        a = 0;
                    }
                } else {
                    uint32_t rr = ((uint32_t)row[x * 6 + 0] << 8) | row[x * 6 + 1];
                    uint32_t gg = ((uint32_t)row[x * 6 + 2] << 8) | row[x * 6 + 3];
                    uint32_t bb = ((uint32_t)row[x * 6 + 4] << 8) | row[x * 6 + 5];
                    r = (uint8_t)(rr >> 8);
                    g = (uint8_t)(gg >> 8);
                    b = (uint8_t)(bb >> 8);
                    if (info->has_trns && rr == info->trns_r &&
                        gg == info->trns_g && bb == info->trns_b) {
                        a = 0;
                    }
                }
            } else if (ct == 3) {
                uint32_t idx = cs_get_sample(row, x, depth);
                if (idx >= info->plte_count) {
                    free(rgba);
                    return NULL;
                }
                r = info->plte[idx][0];
                g = info->plte[idx][1];
                b = info->plte[idx][2];
                if (info->has_trns) {
                    a = (idx < info->trns_count) ? info->trns[idx] : 255u;
                }
            } else if (ct == 4) {
                if (depth == 8) {
                    uint8_t gray = row[x * 2 + 0];
                    r = g = b = gray;
                    a = row[x * 2 + 1];
                } else {
                    r = g = b = row[x * 4 + 0];
                    a = row[x * 4 + 2];
                }
            } else {
                if (depth == 8) {
                    r = row[x * 4 + 0];
                    g = row[x * 4 + 1];
                    b = row[x * 4 + 2];
                    a = row[x * 4 + 3];
                } else {
                    r = row[x * 8 + 0];
                    g = row[x * 8 + 2];
                    b = row[x * 8 + 4];
                    a = row[x * 8 + 6];
                }
            }
            size_t o = ((size_t)y * w + x) * 4;
            rgba[o + 0] = r;
            rgba[o + 1] = g;
            rgba[o + 2] = b;
            rgba[o + 3] = a;
        }
    }
    return rgba;
}

static uint8_t *cs_read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        free(buf);
        return NULL;
    }
    *out_size = (size_t)sz;
    return buf;
}

static const uint8_t cs_png_sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

uint8_t *coreskin_load_png(const char *path, int *w, int *h, int flip_vertically) {
    size_t fsize = 0;
    uint8_t *file = cs_read_file(path, &fsize);
    if (!file) {
        return NULL;
    }
    if (fsize < 8 || memcmp(file, cs_png_sig, 8) != 0) {
        free(file);
        return NULL;
    }
    cs_png_info_t info;
    memset(&info, 0, sizeof(info));
    int have_ihdr = 0;
    uint8_t *idat = NULL;
    size_t idat_len = 0;
    size_t idat_cap = 0;
    size_t pos = 8;
    int saw_iend = 0;
    while (pos + 8 <= fsize) {
        uint32_t clen = cs_read_be32(file + pos);
        const uint8_t *ctype = file + pos + 4;
        if (pos + 12 + (size_t)clen < pos + 12) {
            break;
        }
        if (pos + 12 + (size_t)clen > fsize) {
            break;
        }
        const uint8_t *cdata = file + pos + 8;
        if (memcmp(ctype, "IHDR", 4) == 0) {
            if (clen != 13) {
                free(idat);
                free(file);
                return NULL;
            }
            info.width = cs_read_be32(cdata);
            info.height = cs_read_be32(cdata + 4);
            info.bit_depth = cdata[8];
            info.color_type = cdata[9];
            uint8_t comp = cdata[10];
            uint8_t filt = cdata[11];
            info.interlace = cdata[12];
            if (comp != 0 || filt != 0) {
                free(idat);
                free(file);
                return NULL;
            }
            if (info.width == 0 || info.height == 0) {
                free(idat);
                free(file);
                return NULL;
            }
            have_ihdr = 1;
        } else if (memcmp(ctype, "PLTE", 4) == 0) {
            if (clen % 3 != 0 || clen / 3 > 256) {
                free(idat);
                free(file);
                return NULL;
            }
            info.plte_count = clen / 3;
            for (uint32_t i = 0; i < info.plte_count; i++) {
                info.plte[i][0] = cdata[i * 3 + 0];
                info.plte[i][1] = cdata[i * 3 + 1];
                info.plte[i][2] = cdata[i * 3 + 2];
            }
            info.has_plte = 1;
        } else if (memcmp(ctype, "tRNS", 4) == 0) {
            info.has_trns = 1;
            if (info.color_type == 3) {
                if (clen > 256) {
                    free(idat);
                    free(file);
                    return NULL;
                }
                info.trns_count = clen;
                for (uint32_t i = 0; i < clen; i++) {
                    info.trns[i] = cdata[i];
                }
            } else if (info.color_type == 0) {
                if (clen < 2) {
                    free(idat);
                    free(file);
                    return NULL;
                }
                info.trns_gray = (uint16_t)(((uint32_t)cdata[0] << 8) | cdata[1]);
            } else if (info.color_type == 2) {
                if (clen < 6) {
                    free(idat);
                    free(file);
                    return NULL;
                }
                info.trns_r = (uint16_t)(((uint32_t)cdata[0] << 8) | cdata[1]);
                info.trns_g = (uint16_t)(((uint32_t)cdata[2] << 8) | cdata[3]);
                info.trns_b = (uint16_t)(((uint32_t)cdata[4] << 8) | cdata[5]);
            }
        } else if (memcmp(ctype, "IDAT", 4) == 0) {
            if (idat_len + clen > idat_cap) {
                size_t new_cap = idat_cap ? idat_cap * 2 : 8192;
                while (new_cap < idat_len + clen) {
                    new_cap *= 2;
                }
                uint8_t *grown = realloc(idat, new_cap);
                if (!grown) {
                    free(idat);
                    free(file);
                    return NULL;
                }
                idat = grown;
                idat_cap = new_cap;
            }
            memcpy(idat + idat_len, cdata, clen);
            idat_len += clen;
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            saw_iend = 1;
            break;
        }
        pos += 12 + (size_t)clen;
    }
    if (!have_ihdr || !saw_iend || idat_len == 0) {
        free(idat);
        free(file);
        return NULL;
    }
    uint8_t depth = info.bit_depth;
    uint8_t ct = info.color_type;
    int depth_ok = 0;
    switch (ct) {
        case 0: depth_ok = (depth == 1 || depth == 2 || depth == 4 ||
                            depth == 8 || depth == 16); break;
        case 2: depth_ok = (depth == 8 || depth == 16); break;
        case 3: depth_ok = (depth == 1 || depth == 2 || depth == 4 || depth == 8); break;
        case 4: depth_ok = (depth == 8 || depth == 16); break;
        case 6: depth_ok = (depth == 8 || depth == 16); break;
        default: depth_ok = 0; break;
    }
    if (!depth_ok) {
        free(idat);
        free(file);
        return NULL;
    }
    if (ct == 3 && !info.has_plte) {
        free(idat);
        free(file);
        return NULL;
    }
    if (info.interlace != 0) {
        free(idat);
        free(file);
        return NULL;
    }
    uint32_t channels = cs_channels(ct);
    uint32_t bits_per_pixel = channels * depth;
    uint32_t stride = (info.width * bits_per_pixel + 7u) / 8u;
    uint32_t bpp = (bits_per_pixel + 7u) / 8u;
    if (bpp == 0) {
        bpp = 1;
    }
    size_t raw_cap = (size_t)info.height * (1 + stride);
    uint8_t *raw = malloc(raw_cap);
    if (!raw) {
        free(idat);
        free(file);
        return NULL;
    }
    size_t raw_len = 0;
    if (cs_zlib_inflate(idat, idat_len, raw, raw_cap, &raw_len) != 0) {
        free(raw);
        free(idat);
        free(file);
        return NULL;
    }
    free(idat);
    free(file);
    if (raw_len != raw_cap) {
        free(raw);
        return NULL;
    }
    if (cs_unfilter(raw, info.height, stride, bpp) != 0) {
        free(raw);
        return NULL;
    }
    uint8_t *rgba = cs_expand(raw, &info, stride);
    free(raw);
    if (!rgba) {
        return NULL;
    }
    if (flip_vertically) {
        uint32_t row_bytes = info.width * 4u;
        uint8_t *tmp = malloc(row_bytes);
        if (!tmp) {
            free(rgba);
            return NULL;
        }
        for (uint32_t y = 0; y < info.height / 2; y++) {
            uint8_t *top = rgba + (size_t)y * row_bytes;
            uint8_t *bot = rgba + (size_t)(info.height - 1 - y) * row_bytes;
            memcpy(tmp, top, row_bytes);
            memcpy(top, bot, row_bytes);
            memcpy(bot, tmp, row_bytes);
        }
        free(tmp);
    }
    *w = (int)info.width;
    *h = (int)info.height;
    return rgba;
}
