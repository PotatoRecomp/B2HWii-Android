/* Unpacks the compressed entries of the Basara disc's linkData.afs (header "<packed hex>\t<size hex>\n",
 * data from the next 32-byte boundary). Port of the game's own decoder (803543B0, set up by 8035510C):
 * an order-1 LZP (each previous byte keeps its last 512 match positions and lengths) driven by an
 * adaptive range coder, symbols 0x400.. = literal, 0..0x1FF = new match + length, 0x200..0x3FF =
 * repeat a match with its old length.
 *
 * Build: clang -O2 -o generated/b/b-unpack.exe tools/b-unpack.c
 * Usage: b-unpack ARCHIVE.afs OUTDIR   (writes OUTDIR/NNNNN.bin for every compressed entry)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t range, code;
  const uint8_t *in, *end;
} Coder;

typedef struct {
  Coder* c;
  uint16_t* counts;
  uint32_t total, stage, threshold;
  uint16_t* pairs; /* freq, cum */
  int nsym;
  int ready;
  uint32_t* lookup; /* 0x8000 entries */
} Model;

static uint8_t next_byte(Coder* c) { return c->in < c->end ? *c->in++ : 0; }

static void model_init(Model* m, Coder* c, int nsym) {
  m->c = c;
  m->nsym = nsym;
  m->counts = calloc(nsym, 2);
  m->pairs = calloc(nsym, 4);
  m->lookup = calloc(0x8000, 4);
  for (int i = 0, s = 0; i < 0x8000; ++i) {
    m->counts[s]++;
    if (++s >= nsym) s = 0;
  }
  uint16_t cum = 0;
  for (int s = 0; s < nsym; ++s) {
    m->pairs[2 * s] = m->counts[s];
    m->pairs[2 * s + 1] = cum;
    cum += m->counts[s];
  }
  for (int s = 0; s < nsym; ++s) m->counts[s] = 1;
  m->total = nsym;
  m->stage = 0;
  while (m->stage < 15 && m->total >= (1u << m->stage)) m->stage++;
  m->threshold = 1u << m->stage;
  m->ready = 0;
}

static int model_decode(Model* m) {
  Coder* c = m->c;
  if (c->range <= 0x800000) {
    if (c->range > 0x8000) {
      c->range <<= 8;
      c->code = (c->code << 8) | next_byte(c);
    } else if (c->range > 0x80) {
      c->range <<= 16;
      uint32_t a = next_byte(c), b = next_byte(c);
      c->code = (c->code << 16) | (a << 8) | b;
    } else {
      c->range <<= 24;
      uint32_t a = next_byte(c), b = next_byte(c), d = next_byte(c);
      c->code = (c->code << 24) | (a << 16) | (b << 8) | d;
    }
  }
  c->range >>= 14;
  const uint32_t target = (c->range ? c->code / c->range : 0) & 0xFFFF;
  if (!m->ready) {
    uint32_t pos = 0;
    for (int s = 0; s < m->nsym; ++s) {
      const uint32_t end = (uint32_t)m->pairs[2 * s] + m->pairs[2 * s + 1];
      while (pos < end && pos < 0x8000) m->lookup[pos++] = s;
      if (pos < end) pos = end;
    }
    m->ready = 1;
  }
  const int sym = (int)m->lookup[target & 0x7FFF];
  c->code -= c->range * m->pairs[2 * sym + 1];
  c->range = (c->range * m->pairs[2 * sym]) >> 1;
  m->counts[sym]++;
  m->total++;
  if (m->stage < 15) {
    if (m->total == m->threshold) {
      const uint16_t mult = (uint16_t)(1u << (15 - m->stage));
      uint16_t cum = 0;
      m->ready = 0;
      for (int s = 0; s < m->nsym; ++s) {
        const uint16_t f = (uint16_t)(mult * m->counts[s]);
        m->pairs[2 * s] = f;
        m->pairs[2 * s + 1] = cum;
        cum += f;
      }
      m->stage++;
      m->threshold = 1u << m->stage;
    }
  } else if (m->total >= 0x8000) {
    uint16_t cum = 0;
    uint32_t total = 0;
    m->ready = 0;
    for (int s = 0; s < m->nsym; ++s) {
      uint16_t f = m->counts[s];
      m->pairs[2 * s] = f;
      m->pairs[2 * s + 1] = cum;
      cum += f;
      if (f >= 2) f = (f >> 1) & 0x7FFF;
      m->counts[s] = f;
      total += f;
    }
    m->total = total;
  }
  return sym;
}

typedef struct {
  uint32_t count;
  uint32_t pos[512];
  uint32_t len[512];
} Context;

static int unpack(const uint8_t* src, size_t srcSize, uint8_t* out, uint32_t size) {
  static Context ctx[256];
  memset(ctx, 0, sizeof(ctx));
  Coder c = {0x80, 0, src, src + srcSize};
  c.code = next_byte(&c);
  Model main, bytes;
  model_init(&main, &c, 0x500);
  model_init(&bytes, &c, 0x100);
  uint32_t o = 0, prev = 0;
  int ok = 1;
  while (o < size) {
    const int sym = model_decode(&main);
    uint32_t len;
    if (sym >= 0x400) {
      out[o++] = (uint8_t)(sym - 0x400);
      len = 1;
    } else {
      Context* t = &ctx[out[prev]];
      const uint32_t idx = (t->count + sym) & 0x1FF;
      uint32_t from;
      if (sym < 0x200) {
        uint32_t v = model_decode(&bytes);
        if (v == 2) {
          v = model_decode(&bytes) << 8;
          v |= model_decode(&bytes);
        } else if (v == 1) {
          v = model_decode(&bytes) << 16;
          v |= model_decode(&bytes) << 8;
          v |= model_decode(&bytes);
        } else if (v == 0) {
          v = model_decode(&bytes) << 24;
          v |= model_decode(&bytes) << 16;
          v |= model_decode(&bytes) << 8;
          v |= model_decode(&bytes);
        }
        len = v - 1;
      } else {
        len = t->len[idx];
      }
      from = t->pos[idx];
      if (from >= o || len > size + 0x10000u - o) {
        ok = 0;
        break;
      }
      for (uint32_t k = 0; k < len; ++k) out[o + k] = out[from + k];
      o += len;
    }
    if (o >= 1 && prev < o - 1) {
      Context* t = &ctx[out[prev]];
      t->pos[t->count] = prev + 1;
      t->len[t->count] = len;
      t->count = (t->count + 1) & 0x1FF;
      prev = o - 1;
    }
  }
  free(main.counts), free(main.pairs), free(main.lookup);
  free(bytes.counts), free(bytes.pairs), free(bytes.lookup);
  return ok;
}

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: b-unpack ARCHIVE.afs OUTDIR [FIRST LAST]\n");
    return 2;
  }
  FILE* f = fopen(argv[1], "rb");
  if (!f) return 1;
  uint8_t head[8];
  fread(head, 1, 8, f);
  const uint32_t n = le32(head + 4);
  uint8_t* table = malloc(8 * n);
  fread(table, 1, 8 * n, f);
  const uint32_t first = argc > 3 ? (uint32_t)atoi(argv[3]) : 0;
  const uint32_t last = argc > 4 ? (uint32_t)atoi(argv[4]) : n - 1;
  int done = 0, failed = 0;
  for (uint32_t i = first; i <= last && i < n; ++i) {
    const uint32_t off = le32(table + 8 * i), size = le32(table + 8 * i + 4);
    if (size < 33) continue;
    uint8_t* buf = malloc(size);
    _fseeki64(f, off, SEEK_SET);
    fread(buf, 1, size, f);
    char hdr[33];
    memcpy(hdr, buf, 32);
    hdr[32] = 0;
    char* nl = memchr(hdr, '\n', 32);
    char* tab = memchr(hdr, '\t', 32);
    if (!nl || !tab || tab > nl) {
      free(buf);
      continue;
    }
    char* endp;
    const unsigned long packed = strtoul(hdr, &endp, 16);
    if (endp != tab) {
      free(buf);
      continue;
    }
    const unsigned long unpackedSize = strtoul(tab + 1, &endp, 16);
    if (endp != nl || unpackedSize == 0 || unpackedSize > 0x10000000) {
      free(buf);
      continue;
    }
    const size_t start = ((size_t)(endp - hdr) + 0x20) & ~(size_t)31;
    uint8_t* out = calloc(unpackedSize + 0x10000, 1);
    const size_t avail = size > start ? size - start : 0;
    if (unpack(buf + start, packed < avail ? packed : avail, out, (uint32_t)unpackedSize)) {
      char path[1024];
      snprintf(path, sizeof(path), "%s/%05u.bin", argv[2], i);
      FILE* o = fopen(path, "wb");
      if (o) {
        fwrite(out, 1, unpackedSize, o);
        fclose(o);
      }
      ++done;
    } else {
      fprintf(stderr, "entry %u: bad stream\n", i);
      ++failed;
    }
    free(out);
    free(buf);
  }
  printf("%d unpacked, %d failed\n", done, failed);
  return 0;
}
