// Minimal libretro frontend harness for headless verification of the
// xmil (X Millennium) core, extended for the TapeOS project:
//  - captures the last frame and writes it as a PPM image
//  - searches SYSTEM_RAM for a needle string (boot program signature)
//  - dumps bytes at given RAM addresses (load check)
//  - injects keyboard input from a key-script file: each byte is typed
//    in order (press `key_hold` frames, release `key_gap` frames,
//    starting at frame `key_start`). Byte 0x0A means RETROK_RETURN;
//    byte 0x00 is a rest slot (no key for one hold+gap period, used
//    to let the guest finish printing after an Enter); any other
//    byte is used as the RETROK code directly (printable ASCII
//    codes match RETROK values, 0x08 = RETROK_BACKSPACE).
//
// usage: harness core.so game.d88 sysdir [frames] [fb.ppm] [needle]
//        [dumpaddr] [keysfile|-] [key_start] [key_hold] [key_gap]
//        [dumpaddr2|-] [dumpaddr3|-]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include "libretro.h"

static void *core;
static const char *sysdir;
static unsigned pixfmt = RETRO_PIXEL_FORMAT_RGB565;
static uint8_t *fb_copy = NULL;
static unsigned fb_w = 0, fb_h = 0;
static int frames = 0;

static bool env_cb(unsigned cmd, void *data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data = sysdir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: pixfmt = *(const unsigned*)data; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: { struct retro_variable *v = data; v->value = NULL; return false; }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data = false; return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: return false;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)data = 1; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned*)data = 0; return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
    default: return false;
  }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
  frames++;
  if (!data || data == RETRO_HW_FRAME_BUFFER_VALID) return;
  size_t need = (size_t)w * h * 4;
  if (!fb_copy) fb_copy = malloc(need);
  fb_w = w; fb_h = h;
  const uint8_t *p = data;
  if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
    for (unsigned y = 0; y < h; y++) {
      const uint16_t *row = (const uint16_t*)(p + y * pitch);
      for (unsigned x = 0; x < w; x++) {
        uint16_t v = row[x];
        uint8_t *o = fb_copy + ((size_t)y * w + x) * 4;
        o[0] = (uint8_t)(((v >> 11) & 31) << 3);
        o[1] = (uint8_t)(((v >> 5) & 63) << 2);
        o[2] = (uint8_t)((v & 31) << 3);
        o[3] = 255;
      }
    }
  } else { /* XRGB8888 / RGB888-ish: take bytes as stored (B,G,R,x on LE) */
    unsigned bpp = (pitch >= (size_t)w * 4) ? 4 : 3;
    for (unsigned y = 0; y < h; y++) {
      const uint8_t *row = p + y * pitch;
      for (unsigned x = 0; x < w; x++) {
        const uint8_t *s = row + x * bpp;
        uint8_t *o = fb_copy + ((size_t)y * w + x) * 4;
        if (bpp == 4) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; }
        else { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; }
        o[3] = 255;
      }
    }
  }
}

static void audio_cb(int16_t l, int16_t r){ (void)l;(void)r; }
static size_t audio_batch(const int16_t *d, size_t n){ (void)d; return n; }
static void poll_cb(void){}

/* ---- keyboard injection (M2) ---- */
static uint8_t *key_script = NULL;   /* bytes to type, RETROK codes */
static long key_len = 0;
static long key_start = 300;         /* first frame of typing */
static long key_hold = 4;            /* frames a key stays pressed */
static long key_gap = 4;             /* released frames between keys */

/* The key script holds ASCII bytes, but the core polls RETROK
 * ids and the X1 keyboard is a JIS matrix: an ASCII character is
 * produced by a base key, sometimes with SHIFT. Translate each
 * script byte to (base RETROK id, shift). Ids for printable
 * characters whose RETROK value equals their ASCII code (letters,
 * digits, space, - ^ \ @ [ ; : ] , . / _) fall through unchanged;
 * the core's sdlkbd table maps them to the X1 matrix (the @ [ ; :
 * ] _ entries were added to the core in M6 - before that, and for
 * upper-case letters, those keys could not be typed at all). */
static void key_translate(unsigned char ch, unsigned *id, int *shift) {
  *id = ch;
  *shift = 0;
  if (ch >= 'A' && ch <= 'Z') { *id = (unsigned)(ch - 'A' + 'a'); *shift = 1; return; }
  switch (ch) {
    case '!': *id = '1'; *shift = 1; break;
    case '"': *id = '2'; *shift = 1; break;
    case '#': *id = '3'; *shift = 1; break;
    case '$': *id = '4'; *shift = 1; break;
    case '%': *id = '5'; *shift = 1; break;
    case '&': *id = '6'; *shift = 1; break;
    case '\'': *id = '7'; *shift = 1; break;   /* JIS: shift+7 */
    case '(': *id = '8'; *shift = 1; break;
    case ')': *id = '9'; *shift = 1; break;
    case '=': *id = RETROK_MINUS; *shift = 1; break;
    case '`': *id = RETROK_EQUALS; *shift = 1; break;
    case '|': *id = RETROK_BACKSLASH; *shift = 1; break;
    case '~': *id = RETROK_AT; *shift = 1; break;
    case '{': *id = RETROK_LEFTBRACKET; *shift = 1; break;
    case '+': *id = RETROK_SEMICOLON; *shift = 1; break;
    case '*': *id = RETROK_COLON; *shift = 1; break;
    case '}': *id = RETROK_RIGHTBRACKET; *shift = 1; break;
    case '<': *id = RETROK_COMMA; *shift = 1; break;
    case '>': *id = RETROK_PERIOD; *shift = 1; break;
    case '?': *id = RETROK_SLASH; *shift = 1; break;
    default: break;
  }
}

static int16_t input_cb(unsigned p, unsigned device, unsigned i, unsigned id) {
  (void)p; (void)i;
  if (device != RETRO_DEVICE_KEYBOARD || !key_script) return 0;
  long f = frames;                   /* video_cb for this frame not yet run */
  if (f < key_start) return 0;
  long t = f - key_start;
  long idx = t / (key_hold + key_gap);
  long phase = t % (key_hold + key_gap);
  if (idx >= key_len || phase >= key_hold) return 0;
  if (key_script[idx] == 0) return 0;   /* 0x00 = rest slot (no key) */
  unsigned want_id;
  int want_shift;
  key_translate(key_script[idx], &want_id, &want_shift);
  if (id == RETROK_LSHIFT || id == RETROK_RSHIFT) return want_shift ? 1 : 0;
  return id == want_id ? 1 : 0;
}

#define LOAD(var, sym) do{ *(void**)&var = dlsym(core, sym); if(!var){fprintf(stderr,"missing %s\n", sym); return 1;} }while(0)

static void dump16(uint8_t *ram, size_t ramsz, unsigned addr) {
  if (addr + 16 > ramsz) return;
  printf("ram[0x%04x]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
         addr,
         ram[addr+0], ram[addr+1], ram[addr+2], ram[addr+3],
         ram[addr+4], ram[addr+5], ram[addr+6], ram[addr+7],
         ram[addr+8], ram[addr+9], ram[addr+10], ram[addr+11],
         ram[addr+12], ram[addr+13], ram[addr+14], ram[addr+15]);
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: harness core.so game.d88 sysdir [frames] [fb.ppm] [needle] [dumpaddr] [keysfile|-] [key_start] [key_hold] [key_gap] [dumpaddr2|-] [dumpaddr3|-]\n"); return 2; }
  sysdir = argv[3];
  int run_frames = argc > 4 ? atoi(argv[4]) : 300;
  const char *ppm_path = argc > 5 ? argv[5] : NULL;
  const char *needle = argc > 6 ? argv[6] : NULL;
  unsigned dumpaddr = argc > 7 ? (unsigned)strtol(argv[7], NULL, 0) : 0x2000;
  const char *keys_path = argc > 8 ? argv[8] : NULL;
  if (argc > 9) key_start = strtol(argv[9], NULL, 0);
  if (argc > 10) key_hold = strtol(argv[10], NULL, 0);
  if (argc > 11) key_gap = strtol(argv[11], NULL, 0);
  unsigned dumpaddr2 = (argc > 12 && strcmp(argv[12], "-")) ? (unsigned)strtol(argv[12], NULL, 0) : (unsigned)-1;
  unsigned dumpaddr3 = (argc > 13 && strcmp(argv[13], "-")) ? (unsigned)strtol(argv[13], NULL, 0) : (unsigned)-1;

  if (keys_path && strcmp(keys_path, "-")) {
    FILE *kf = fopen(keys_path, "rb");
    if (!kf) { fprintf(stderr, "cannot open keys file %s\n", keys_path); return 2; }
    fseek(kf, 0, SEEK_END); key_len = ftell(kf); fseek(kf, 0, SEEK_SET);
    key_script = malloc(key_len ? key_len : 1);
    if (key_len && fread(key_script, 1, key_len, kf) != (size_t)key_len) return 1;
    fclose(kf);
    for (long k = 0; k < key_len; k++)
      if (key_script[k] == 0x0A) key_script[k] = 13;   /* LF -> RETROK_RETURN */
    printf("keys=%ld start=%ld hold=%ld gap=%ld\n", key_len, key_start, key_hold, key_gap);
  }

  core = dlopen(argv[1], RTLD_NOW);
  if (!core) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
  typedef void (*set_env_t)(retro_environment_t); typedef void (*set_video_t)(retro_video_refresh_t);
  typedef void (*set_audio_t)(retro_audio_sample_t); typedef void (*set_batch_t)(retro_audio_sample_batch_t);
  typedef void (*set_poll_t)(retro_input_poll_t); typedef void (*set_input_t)(retro_input_state_t);
  typedef void (*rinit_t)(void); typedef void (*rrun_t)(void); typedef bool (*rload_t)(const struct retro_game_info*);
  typedef void *(*getmem_t)(unsigned); typedef size_t (*getmemsize_t)(unsigned);
  set_env_t set_env; set_video_t set_video; set_audio_t set_audio; set_batch_t set_batch;
  set_poll_t set_poll; set_input_t set_input;
  rinit_t rinit; rrun_t rrun; rload_t rload; getmem_t getmem; getmemsize_t getmemsize;
  LOAD(set_env,"retro_set_environment"); LOAD(set_video,"retro_set_video_refresh"); LOAD(set_audio,"retro_set_audio_sample"); LOAD(set_batch,"retro_set_audio_sample_batch"); LOAD(set_poll,"retro_set_input_poll"); LOAD(set_input,"retro_set_input_state");
  LOAD(rinit,"retro_init"); LOAD(rrun,"retro_run"); LOAD(rload,"retro_load_game"); LOAD(getmem,"retro_get_memory_data"); LOAD(getmemsize,"retro_get_memory_size");
  set_env(env_cb); set_video(video_cb); set_audio(audio_cb); set_batch(audio_batch); set_poll(poll_cb); set_input(input_cb);
  rinit();
  struct retro_game_info gi = {0};
  gi.path = argv[2];
  FILE *f = fopen(argv[2], "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  void *buf = malloc(sz); if (fread(buf, 1, sz, f) != (size_t)sz) return 1; fclose(f);
  gi.data = buf; gi.size = sz;
  if (!rload(&gi)) { printf("LOAD_FAILED\n"); return 3; }
  unsigned flagaddr = (argc > 15 && strcmp(argv[15], "-")) ? (unsigned)strtol(argv[15], NULL, 0) : (unsigned)-1;
  unsigned flagvalue = argc > 16 ? (unsigned)strtol(argv[16], NULL, 0) : 0;
  uint8_t *ram0 = getmem(RETRO_MEMORY_SYSTEM_RAM);
  size_t ramsz0 = getmemsize(RETRO_MEMORY_SYSTEM_RAM);
  long flagframe = -1;
  for (int i = 0; i < run_frames; i++) {
    rrun();
    if (flagaddr != (unsigned)-1 && ram0 && flagaddr < ramsz0 &&
        ram0[flagaddr] == (uint8_t)flagvalue) { flagframe = frames; break; }
  }
  printf("flag_reached=%s flag_frame=%ld\n", flagframe >= 0 ? "yes" : "no", flagframe);

  printf("frames=%d fb=%ux%u pixfmt=%u\n", frames, fb_w, fb_h, pixfmt);

  if (ppm_path && fb_copy) {
    FILE *o = fopen(ppm_path, "wb");
    fprintf(o, "P6\n%u %u\n255\n", fb_w, fb_h);
    for (size_t i = 0; i < (size_t)fb_w * fb_h; i++) fwrite(fb_copy + i * 4, 1, 3, o);
    fclose(o);
    printf("ppm_written=%s\n", ppm_path);
  }

  uint8_t *ram = getmem(RETRO_MEMORY_SYSTEM_RAM);
  size_t ramsz = getmemsize(RETRO_MEMORY_SYSTEM_RAM);
  printf("ram_size=%zu\n", ramsz);
  if (ram) {
    if (needle) {
      size_t nl = strlen(needle);
      long found = -1;
      for (size_t i = 0; i + nl <= ramsz; i++) if (!memcmp(ram + i, needle, nl)) { found = (long)i; break; }
      printf("needle \"%s\" %s", needle, found >= 0 ? "FOUND" : "NOT_FOUND");
      if (found >= 0) printf(" at 0x%04lx", found);
      printf("\n");
    }
    dump16(ram, ramsz, dumpaddr);
    if (dumpaddr2 != (unsigned)-1) dump16(ram, ramsz, dumpaddr2);
    if (dumpaddr3 != (unsigned)-1) dump16(ram, ramsz, dumpaddr3);
  }
  return 0;
}
