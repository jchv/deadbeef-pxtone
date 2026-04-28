#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deadbeef/deadbeef.h>

#include "pxtone/pxtnError.h"
#include "pxtone/pxtnService.h"

#define trace(...)                                                             \
  {                                                                            \
    fprintf(stderr, __VA_ARGS__);                                              \
  }

static DB_functions_t *deadbeef;
static ddb_decoder2_t plugin;

#define DEFAULT_LOOP_COUNT 2
#define DEFAULT_FADE_DURATION 10.0
#define DEFAULT_FADE_DELAY 0.0
#define DEFAULT_LOOP_COUNT_STR "2"
#define DEFAULT_FADE_DURATION_STR "10.0"
#define DEFAULT_FADE_DELAY_STR "0.0"

static int conf_loop_single = 0;
static int conf_loop_count = DEFAULT_LOOP_COUNT;
static double conf_fade_duration = DEFAULT_FADE_DURATION;
static double conf_fade_delay = DEFAULT_FADE_DELAY;

static const char settings_dlg[] =
    "property \"Loop count\" entry pxtone.loopcount " DEFAULT_LOOP_COUNT_STR
    ";\n"
    "property \"Fade duration (seconds)\" entry "
    "pxtone.fadeduration " DEFAULT_FADE_DURATION_STR ";\n"
    "property \"Fade delay (seconds)\" entry "
    "pxtone.fadedelay " DEFAULT_FADE_DELAY_STR ";\n";

struct pxtone_io {
  DB_FILE *file;
};

static bool pxtn_read(void *user, void *p_dst, int32_t size, int32_t num) {
  pxtone_io *io = (pxtone_io *)user;
  return (int32_t)deadbeef->fread(p_dst, size, num, io->file) == num;
}

static bool pxtn_write(void *user, const void *p_dst, int32_t size,
                       int32_t num) {
  (void)user;
  (void)p_dst;
  (void)size;
  (void)num;
  return false;
}

static bool pxtn_seek(void *user, int mode, int32_t size) {
  pxtone_io *io = (pxtone_io *)user;
  return deadbeef->fseek(io->file, size, mode) == 0;
}

static bool pxtn_pos(void *user, int32_t *p_pos) {
  pxtone_io *io = (pxtone_io *)user;
  int64_t pos = deadbeef->ftell(io->file);
  if (pos < 0)
    return false;
  *p_pos = (int32_t)pos;
  return true;
}

typedef struct {
  DB_fileinfo_t info;
  pxtnService *pxtn;
  int position;
  int total_play_samples;
  int loop_start;
  int loop_body;
  int fade_start;
  int fade_samples;
} pxtone_info_t;

static pxtnService *open_pxtone(const char *fname, int samplerate) {
  DB_FILE *fp = deadbeef->fopen(fname);
  if (!fp)
    return NULL;

  pxtone_io io;
  io.file = fp;

  pxtnService *pxtn =
      new pxtnService(pxtn_read, pxtn_write, pxtn_seek, pxtn_pos);
  pxtnERR err = pxtn->init();
  if (err != pxtnOK) {
    trace("pxtone: init failed: %s\n", pxtnError_get_string(err));
    delete pxtn;
    deadbeef->fclose(fp);
    return NULL;
  }

  if (!pxtn->set_destination_quality(2, samplerate)) {
    trace("pxtone: set_destination_quality failed\n");
    delete pxtn;
    deadbeef->fclose(fp);
    return NULL;
  }

  err = pxtn->read(&io);
  if (err != pxtnOK) {
    trace("pxtone: read failed: %s\n", pxtnError_get_string(err));
    delete pxtn;
    deadbeef->fclose(fp);
    return NULL;
  }

  err = pxtn->tones_ready();
  if (err != pxtnOK) {
    trace("pxtone: tones_ready failed: %s\n", pxtnError_get_string(err));
    delete pxtn;
    deadbeef->fclose(fp);
    return NULL;
  }

  deadbeef->fclose(fp);
  return pxtn;
}

static int calc_loop_start_sample(pxtnService *pxtn) {
  int32_t repeat_meas = pxtn->master->get_repeat_meas();
  int32_t beat_num = pxtn->master->get_beat_num();
  float beat_tempo = pxtn->master->get_beat_tempo();
  int32_t beat_clock = pxtn->master->get_beat_clock();
  int32_t sps;
  pxtn->get_destination_quality(NULL, &sps);
  float clock_rate =
      (float)(60.0f * (double)sps / ((double)beat_tempo * (double)beat_clock));
  return (int32_t)((double)repeat_meas * (double)beat_num * (double)beat_clock *
                   clock_rate);
}

static int calc_total_play_samples(pxtnService *pxtn, int loop_count,
                                   double fade_duration, double fade_delay) {
  int32_t total = pxtn->moo_get_total_sample();
  int32_t sps;
  pxtn->get_destination_quality(NULL, &sps);
  int32_t loop_start = calc_loop_start_sample(pxtn);
  int32_t repeat_meas = pxtn->master->get_repeat_meas();

  if (repeat_meas > 0 && loop_count > 0) {
    int32_t loop_body = total - loop_start;
    if (loop_body > 0) {
      return total + loop_body * (loop_count - 1) + (int)(fade_delay * sps) +
             (int)(fade_duration * sps);
    }
  }
  return total;
}

static void apply_fade(int16_t *buf, int sample_count, int channels,
                       int position, int fade_start, int fade_samples) {
  if (fade_samples <= 0) {
    return;
  }
  for (int i = 0; i < sample_count; i++) {
    int pos = position + i;
    if (pos < fade_start) {
      continue;
    }
    float t = (float)(pos - fade_start) / (float)fade_samples;
    if (t >= 1.0f)
      t = 1.0f;
    float gain = 1.0f - t;
    gain = gain * gain; // simple approximation of log fade
    for (int ch = 0; ch < channels; ch++) {
      buf[i * channels + ch] = (int16_t)(buf[i * channels + ch] * gain);
    }
  }
}

// Map a logical output position to the pxtone engine sample position.
static int32_t logical_to_pxtone_pos(pxtone_info_t *info, int64_t pos) {
  int32_t song_end = info->loop_start + info->loop_body;
  if (info->loop_body <= 0 || pos < song_end)
    return (int32_t)pos;
  return (int32_t)(info->loop_start + ((pos - song_end) % info->loop_body));
}

static DB_fileinfo_t *pxtone_open(uint32_t hints) {
  pxtone_info_t *info = (pxtone_info_t *)calloc(1, sizeof(pxtone_info_t));
  return &info->info;
}

static int pxtone_init(DB_fileinfo_t *_info, DB_playItem_t *it) {
  pxtone_info_t *info = (pxtone_info_t *)_info;

  deadbeef->pl_lock();
  char *fname = strdup(deadbeef->pl_find_meta(it, ":URI"));
  deadbeef->pl_unlock();

  int samplerate = 44100;
  info->pxtn = open_pxtone(fname, samplerate);
  free(fname);
  if (!info->pxtn)
    return -1;

  int32_t repeat_meas = info->pxtn->master->get_repeat_meas();
  bool has_loop = repeat_meas > 0;
  int32_t song_samples = info->pxtn->moo_get_total_sample();

  info->loop_start = has_loop ? calc_loop_start_sample(info->pxtn) : 0;
  info->loop_body = has_loop ? song_samples - info->loop_start : 0;

  if (conf_loop_single && has_loop) {
    info->total_play_samples = 0;
    info->fade_start = 0;
    info->fade_samples = 0;
  } else if (has_loop && conf_loop_count > 0) {
    info->total_play_samples = calc_total_play_samples(
        info->pxtn, conf_loop_count, conf_fade_duration, conf_fade_delay);
    info->fade_samples = (int)(conf_fade_duration * samplerate);
    info->fade_start = info->total_play_samples - info->fade_samples;
  } else {
    info->total_play_samples = song_samples;
    info->fade_start = 0;
    info->fade_samples = 0;
  }

  // Always let pxtone loop; we control ending ourselves.
  pxtnVOMITPREPARATION prep = {0};
  prep.flags = has_loop ? pxtnVOMITPREPFLAG_loop : 0;
  prep.master_volume = 0.8f;
  if (!info->pxtn->moo_preparation(&prep)) {
    trace("pxtone: moo_preparation failed\n");
    delete info->pxtn;
    info->pxtn = NULL;
    return -1;
  }

  _info->readpos = 0;
  _info->plugin = &plugin.decoder;
  _info->fmt.bps = 16;
  _info->fmt.channels = 2;
  _info->fmt.samplerate = samplerate;
  _info->fmt.channelmask = 0x3;

  if (info->total_play_samples > 0) {
    deadbeef->pl_lock();
    int num_playlists = deadbeef->plt_get_count();
    for (int i = 0; i < num_playlists; i++) {
      ddb_playlist_t *plt = deadbeef->plt_get_for_idx(i);
      if (!plt || deadbeef->plt_get_item_idx(plt, it, 0) == -1) {
        deadbeef->plt_unref(plt);
        continue;
      }
      deadbeef->plt_set_item_duration(
          plt, it, (float)info->total_play_samples / samplerate);
      deadbeef->plt_unref(plt);
      break;
    }
    deadbeef->pl_unlock();
  }

  return 0;
}

static void pxtone_free(DB_fileinfo_t *_info) {
  pxtone_info_t *info = (pxtone_info_t *)_info;
  delete info->pxtn;
  free(info);
}

static int pxtone_read(DB_fileinfo_t *_info, char *bytes, int size) {
  pxtone_info_t *info = (pxtone_info_t *)_info;
  int sample_size = _info->fmt.channels * sizeof(int16_t);
  int samples_requested = size / sample_size;

  if (info->total_play_samples > 0) {
    int remaining = info->total_play_samples - info->position;
    if (remaining <= 0)
      return 0;
    if (samples_requested > remaining)
      samples_requested = remaining;
  }

  int render_bytes = samples_requested * sample_size;

  if (info->pxtn->moo_is_end_vomit())
    return 0;
  if (!info->pxtn->Moo(bytes, render_bytes))
    return 0;

  if (info->fade_samples > 0)
    apply_fade((int16_t *)bytes, samples_requested, _info->fmt.channels,
               info->position, info->fade_start, info->fade_samples);

  info->position += samples_requested;
  _info->readpos = (float)info->position / (float)_info->fmt.samplerate;

  return render_bytes;
}

static int pxtone_seek_sample64(DB_fileinfo_t *_info, int64_t sample) {
  pxtone_info_t *info = (pxtone_info_t *)_info;

  if (info->total_play_samples > 0 && sample > info->total_play_samples)
    sample = info->total_play_samples;

  pxtnVOMITPREPARATION prep = {0};
  prep.start_pos_sample = logical_to_pxtone_pos(info, sample);
  prep.master_volume = 0.8f;

  bool has_loop = info->loop_body > 0;
  prep.flags = has_loop ? pxtnVOMITPREPFLAG_loop : 0;

  info->pxtn->moo_preparation(&prep);
  info->position = (int)sample;
  _info->readpos = (float)info->position / (float)_info->fmt.samplerate;
  return 0;
}

static int pxtone_seek_sample(DB_fileinfo_t *_info, int sample) {
  return pxtone_seek_sample64(_info, sample);
}

static int pxtone_seek(DB_fileinfo_t *_info, float time) {
  return pxtone_seek_sample64(
      _info, (int64_t)((double)time * (double)_info->fmt.samplerate));
}

static int pxtone_numvoices(DB_fileinfo_t *_info) {
  pxtone_info_t *info = (pxtone_info_t *)_info;
  return info->pxtn ? info->pxtn->Unit_Num() : 0;
}

static void pxtone_mutevoice(DB_fileinfo_t *_info, int voice, int mute) {
  pxtone_info_t *info = (pxtone_info_t *)_info;
  if (!info->pxtn || voice < 0 || voice >= info->pxtn->Unit_Num())
    return;
  pxtnUnit *u = info->pxtn->Unit_Get_variable(voice);
  u->set_played(!mute);
  info->pxtn->moo_set_mute_by_unit(true);
}

static DB_playItem_t *pxtone_insert(ddb_playlist_t *plt, DB_playItem_t *after,
                                    const char *fname) {
  pxtnService *pxtn = open_pxtone(fname, 44100);
  if (!pxtn)
    return after;

  DB_playItem_t *it =
      deadbeef->pl_item_alloc_init(fname, plugin.decoder.plugin.id);

  int32_t name_size = 0;
  const char *title = pxtn->text->get_name_buf(&name_size);
  if (title && name_size > 0)
    deadbeef->pl_add_meta(it, "title", title);

  int32_t comment_size = 0;
  const char *comment = pxtn->text->get_comment_buf(&comment_size);
  if (comment && comment_size > 0)
    deadbeef->pl_add_meta(it, "comment", comment);

  deadbeef->pl_replace_meta(it, ":FILETYPE", "pxtone");

  float beat_tempo = pxtn->master->get_beat_tempo();
  int32_t meas_num = pxtn->master->get_meas_num();
  int32_t repeat_meas = pxtn->master->get_repeat_meas();

  char bpm_str[32];
  snprintf(bpm_str, sizeof(bpm_str), "%.1f", beat_tempo);
  deadbeef->pl_add_meta(it, "BPM", bpm_str);

  deadbeef->pl_set_meta_int(it, ":SAMPLERATE", 44100);
  deadbeef->pl_set_meta_int(it, ":CHANNELS", 2);
  deadbeef->pl_set_meta_int(it, ":BPS", 16);

  if (repeat_meas > 0) {
    int32_t loop_start_smp = calc_loop_start_sample(pxtn);
    int32_t total_smp = pxtn->moo_get_total_sample();
    deadbeef->pl_set_meta_int(it, ":loop_start", loop_start_smp);
    deadbeef->pl_set_meta_int(it, ":loop_end", total_smp);
  }

  deadbeef->pl_set_meta_int(it, "pxtone_units", pxtn->Unit_Num());
  deadbeef->pl_set_meta_int(it, "pxtone_voices", pxtn->Woice_Num());
  deadbeef->pl_set_meta_int(it, "pxtone_measures", meas_num);

  int total = calc_total_play_samples(pxtn, conf_loop_count, conf_fade_duration,
                                      conf_fade_delay);
  deadbeef->plt_set_item_duration(plt, it, (float)total / 44100.0f);

  after = deadbeef->plt_insert_item(plt, after, it);
  deadbeef->pl_item_unref(it);

  delete pxtn;
  return after;
}

static void pxtone_reload_config(void) {
  conf_loop_single = deadbeef->conf_get_int("playback.loop", DDB_REPEAT_ALL) ==
                     DDB_REPEAT_SINGLE;
  conf_loop_count =
      deadbeef->conf_get_int("pxtone.loopcount", DEFAULT_LOOP_COUNT);
  conf_fade_duration = (double)deadbeef->conf_get_float(
      "pxtone.fadeduration", (float)DEFAULT_FADE_DURATION);
  conf_fade_delay = (double)deadbeef->conf_get_float("pxtone.fadedelay",
                                                     (float)DEFAULT_FADE_DELAY);
}

static int pxtone_start(void) {
  pxtone_reload_config();
  return 0;
}

static int pxtone_stop(void) { return 0; }

static int pxtone_message(uint32_t id, uintptr_t ctx, uint32_t p1,
                          uint32_t p2) {
  switch (id) {
  case DB_EV_CONFIGCHANGED:
    pxtone_reload_config();
    break;
  }
  return 0;
}

static const char *exts[] = {"ptcop", "pttune", NULL};

#define COPYRIGHT_STR                                                          \
  "deadbeef-pxtone\n"                                                          \
  "Copyright (c) 2026 John Chadwick <john@jchw.io>\n"                          \
  "\n"                                                                         \
  "Permission to use, copy, modify, and distribute this software for any\n"    \
  "purpose with or without fee is hereby granted, provided that the above\n"   \
  "copyright notice and this permission notice appear in all copies.\n"        \
  "\n"                                                                         \
  "THE SOFTWARE IS PROVIDED \"AS IS\" AND THE AUTHOR DISCLAIMS ALL "           \
  "WARRANTIES\n"                                                               \
  "WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF\n"         \
  "MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR\n"  \
  "ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES\n"   \
  "WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN\n"    \
  "ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF\n"  \
  "OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.\n"           \
  "\n"                                                                         \
  "Uses pxtone library by STUDIO PIXEL.\n"                                     \
  "https://pxtone.org/developer/"

extern "C" __attribute__((visibility("default")))
DB_plugin_t *pxtone_load(DB_functions_t *api) {
  deadbeef = api;

  memset(&plugin, 0, sizeof(plugin));
  plugin.decoder.plugin.api_vmajor = DB_API_VERSION_MAJOR;
  plugin.decoder.plugin.api_vminor = DB_API_VERSION_MINOR;
  plugin.decoder.plugin.version_major = 0;
  plugin.decoder.plugin.version_minor = 1;
  plugin.decoder.plugin.flags = DDB_PLUGIN_FLAG_IMPLEMENTS_DECODER2;
  plugin.decoder.plugin.type = DB_PLUGIN_DECODER;
  plugin.decoder.plugin.name = "pxtone";
  plugin.decoder.plugin.id = "pxtone";
  plugin.decoder.plugin.descr = "pxtone Collage music decoder";
  plugin.decoder.plugin.copyright = COPYRIGHT_STR;
  plugin.decoder.plugin.start = pxtone_start;
  plugin.decoder.plugin.stop = pxtone_stop;
  plugin.decoder.plugin.configdialog = settings_dlg;
  plugin.decoder.plugin.message = pxtone_message;
  plugin.decoder.open = pxtone_open;
  plugin.decoder.init = pxtone_init;
  plugin.decoder.free = pxtone_free;
  plugin.decoder.read = pxtone_read;
  plugin.decoder.seek = pxtone_seek;
  plugin.decoder.seek_sample = pxtone_seek_sample;
  plugin.decoder.insert = pxtone_insert;
  plugin.decoder.numvoices = pxtone_numvoices; // TODO: Not used...?
  plugin.decoder.mutevoice = pxtone_mutevoice;
  plugin.decoder.exts = exts;
  plugin.seek_sample64 = pxtone_seek_sample64;

  return DB_PLUGIN(&plugin);
}
