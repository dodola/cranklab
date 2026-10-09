#include "pd_runtime.h"
#include "pd_pdx.h"
#include <unistd.h>

#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define META_SAMPLE "pd.audiosample"
#define META_SAMPLEPLAYER "pd.sampleplayer"
#define META_FILEPLAYER "pd.fileplayer"
#define META_SYNTH "pd.synth"
#define META_CHANNEL "pd.channel"

static int audio_ready = 0;
static int host_suspended = 0;
#ifdef CRANK_EXTERNAL_SDL
static SDL_atomic_t host_volume = { 100 };
/* A device-wide postmix gain preserves each game's own player/channel volume. */
void crank_switch_volume(int percent) {
    SDL_AtomicSet(&host_volume, percent<0 ? 0 : percent>100 ? 100 : percent);
}
#endif
static void capture_mix(void *unused, Uint8 *stream, int bytes) {
#ifdef CRANK_EXTERNAL_SDL
    int volume=SDL_AtomicGet(&host_volume);
    if(volume!=100) {
        int16_t *samples=(int16_t*)stream;
        for(int i=0;i<bytes/(int)sizeof(int16_t);i++) samples[i]=(int16_t)((int)samples[i]*volume/100);
    }
#endif
    pd_host_audio((const int16_t *)stream, bytes / (int)sizeof(int16_t));
}

void pd_sound_shutdown(void) {
    if (audio_ready > 0) { Mix_SetPostMix(NULL, NULL); Mix_CloseAudio(); }
    audio_ready = 0;
    host_suspended = 0;
}

void pd_sound_set_suspended(int suspended) {
    host_suspended = suspended != 0;
    /* Pause the device, preserving each channel's own playing/paused state. */
    if (audio_ready > 0) {
#if SDL_MIXER_VERSION_ATLEAST(2,6,0)
        Mix_PauseAudio(host_suspended);
#else
        /* devkitPro's SDL_mixer 2.0 uses SDL's legacy default audio device. */
        SDL_PauseAudio(host_suspended);
#endif
    }
}

static void ensure_audio(void) {
    if (audio_ready) return;
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
        Mix_AllocateChannels(32);
        if (pd_host_active()) Mix_SetPostMix(capture_mix, NULL);
        audio_ready = 1;
        if (host_suspended) pd_sound_set_suspended(1);
    } else {
        fprintf(stderr, "sound: Mix_OpenAudio failed: %s\n", Mix_GetError());
        audio_ready = -1;
    }
}

typedef struct {
    Mix_Chunk *chunk;
} PdSample;

typedef struct {
    Mix_Chunk *chunk;
    Mix_Music *music;
    int channel;
    int playing;
    int paused;
    float volume;
    float right_volume;
    float rate;             /* playback rate; 1.0 = normal pitch */
    Mix_Chunk *rate_chunk;  /* cached resampled copy for rate != 1 */
    float rate_chunk_rate;
    Mix_Chunk *rate_src;    /* chunk the cache was built from */
    uint32_t start_ticks;   /* SDL ticks when playback started */
    uint32_t paused_ticks;  /* SDL ticks when paused */
} PdPlayer;

/* Nearest-neighbor resample of a device-format chunk by a rate factor
   (rate 2.0 = double speed / +1 octave). Returns a new chunk or NULL. */
static Mix_Chunk *resample_chunk(Mix_Chunk *src, float rate) {
    if (!src || !src->abuf || rate <= 0) return NULL;
    int freq = 44100, chans = 2;
    Uint16 fmt = AUDIO_S16LSB;
    Mix_QuerySpec(&freq, &fmt, &chans);
    int bytes_per_sample = SDL_AUDIO_BITSIZE(fmt) / 8;
    int bpf = chans * bytes_per_sample;
    if (bpf <= 0 || bytes_per_sample != 2) return NULL;
    long in_frames = src->alen / bpf;
    long out_frames = (long)((double)in_frames / rate);
    if (out_frames < 1) return NULL;
    int16_t *out = malloc((size_t)out_frames * bpf);
    if (!out) return NULL;
    const int16_t *in = (const int16_t *)src->abuf;
    for (long f = 0; f < out_frames; f++) {
        long sf = (long)((double)f * rate);
        if (sf >= in_frames) sf = in_frames - 1;
        for (int c = 0; c < chans; c++)
            out[f * chans + c] = in[sf * chans + c];
    }
    Mix_Chunk *ck = Mix_QuickLoad_RAW((Uint8 *)out, (Uint32)(out_frames * bpf));
    if (!ck) free(out);
    return ck;
}

/* Resolve the chunk to actually play, resampling for rate changes. */
static Mix_Chunk *player_effective_chunk(PdPlayer *p) {
    if (!p || !p->chunk) return NULL;
    float rate = p->rate > 0 ? p->rate : 1.0f;
    if (rate > 0.999f && rate < 1.001f) return p->chunk;
    if (p->rate_chunk && p->rate_chunk_rate == rate && p->rate_src == p->chunk)
        return p->rate_chunk;
    Mix_Chunk *ck = resample_chunk(p->chunk, rate);
    if (!ck) return p->chunk;
    if (p->rate_chunk) {
        free(p->rate_chunk->abuf);
        Mix_FreeChunk(p->rate_chunk);
    }
    p->rate_chunk = ck;
    p->rate_chunk_rate = rate;
    p->rate_src = p->chunk;
    return p->rate_chunk;
}

/* ---- .pda (Playdate AUD) loader ---- */

static const int ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

typedef struct {
    int predictor;
    int step_index;
} ImaState;

static int16_t ima_decode_nibble(ImaState *st, int nibble) {
    int step = ima_step_table[st->step_index];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) diff = -diff;
    st->predictor += diff;
    if (st->predictor > 32767) st->predictor = 32767;
    if (st->predictor < -32768) st->predictor = -32768;
    st->step_index += ima_index_table[nibble & 0xF];
    if (st->step_index < 0) st->step_index = 0;
    if (st->step_index > 88) st->step_index = 88;
    return (int16_t)st->predictor;
}

/* Decode a .pda file to signed 16-bit PCM. Returns malloc'd buffer. */
static int16_t *pda_decode(const uint8_t *data, long len, int *out_samples,
                           int *out_channels, int *out_rate) {
    if (len < 16 || memcmp(data, "Playdate AUD", 12) != 0) return NULL;
    int rate = data[12] | (data[13] << 8) | (data[14] << 16);
    int fmt = data[15];
    const uint8_t *body = data + 16;
    long body_len = len - 16;
    int channels = (fmt == 1 || fmt == 3 || fmt == 5) ? 2 : 1;
    int16_t *pcm = NULL;
    long nsamples = 0; /* per channel */

    if (fmt == 0 || fmt == 1) { /* 8-bit PCM (signed) */
        nsamples = body_len / channels;
        pcm = malloc((size_t)nsamples * channels * 2);
        for (long i = 0; i < nsamples * channels; i++)
            pcm[i] = (int16_t)(((int16_t)(int8_t)body[i]) << 8);
    } else if (fmt == 2 || fmt == 3) { /* 16-bit PCM */
        nsamples = body_len / 2 / channels;
        pcm = malloc((size_t)nsamples * channels * 2);
        memcpy(pcm, body, (size_t)nsamples * channels * 2);
    } else if (fmt == 4 || fmt == 5) { /* IMA ADPCM */
        if (body_len < 2) return NULL;
        int block_size = body[0] | (body[1] << 8);
        if (block_size <= 4 * channels) return NULL;
        const uint8_t *p = body + 2;
        long remaining = body_len - 2;
        long data_per_block = block_size - 4 * channels;
        long samples_per_block = 1 + (channels == 1 ? data_per_block * 2 : data_per_block);
        long nblocks = (remaining + block_size - 1) / block_size;
        long cap = nblocks * samples_per_block;
        pcm = malloc((size_t)cap * channels * 2);
        long out = 0; /* interleaved sample frames */
        while (remaining >= 4 * channels) {
            long this_block = remaining < block_size ? remaining : block_size;
            ImaState st[2];
            for (int c = 0; c < channels; c++) {
                st[c].predictor = (int16_t)(p[0] | (p[1] << 8));
                st[c].step_index = p[2];
                if (st[c].step_index > 88) st[c].step_index = 88;
                p += 4;
            }
            long dlen = this_block - 4 * channels;
            /* first sample = predictor */
            for (int c = 0; c < channels; c++)
                pcm[out * channels + c] = (int16_t)st[c].predictor;
            out++;
            if (channels == 1) {
                /* high nibble first */
                for (long i = 0; i < dlen; i++) {
                    pcm[out++] = ima_decode_nibble(&st[0], (p[i] >> 4) & 0xF);
                    pcm[out++] = ima_decode_nibble(&st[0], p[i] & 0xF);
                }
            } else {
                for (long i = 0; i < dlen; i++) {
                    /* left = high nibble, right = low nibble */
                    pcm[out * 2] = ima_decode_nibble(&st[0], (p[i] >> 4) & 0xF);
                    pcm[out * 2 + 1] = ima_decode_nibble(&st[1], p[i] & 0xF);
                    out++;
                }
            }
            p += dlen;
            remaining -= this_block;
        }
        nsamples = channels == 1 ? out : out;
        if (channels == 1) nsamples = out;
    } else {
        return NULL;
    }

    *out_samples = (int)nsamples;
    *out_channels = channels;
    *out_rate = rate > 0 ? rate : 44100;
    return pcm;
}

static Mix_Chunk *load_pda_chunk(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    int nsamples = 0, channels = 0, rate = 0;
    int16_t *pcm = pda_decode(data, sz, &nsamples, &channels, &rate);
    free(data);
    if (!pcm) return NULL;

    int dev_freq;
    Uint16 dev_fmt;
    int dev_chans;
    if (!Mix_QuerySpec(&dev_freq, &dev_fmt, &dev_chans)) {
        free(pcm);
        return NULL;
    }
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, AUDIO_S16LSB, (Uint8)channels, rate,
                          dev_fmt, (Uint8)dev_chans, dev_freq) < 0) {
        free(pcm);
        return NULL;
    }
    long src_len = (long)nsamples * channels * 2;
    cvt.len = (int)src_len;
    cvt.buf = malloc((size_t)src_len * (cvt.len_mult > 0 ? cvt.len_mult : 1));
    memcpy(cvt.buf, pcm, src_len);
    free(pcm);
    if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) {
        free(cvt.buf);
        return NULL;
    }
    Mix_Chunk *chunk = Mix_QuickLoad_RAW(cvt.buf, (Uint32)(cvt.needed ? cvt.len_cvt : cvt.len));
    /* cvt.buf intentionally kept alive for the chunk's lifetime */
    return chunk;
}

/* Resolve an audio path against the game dir, trying .pda and .wav.
 * pdc renames source audio (foo.wav -> foo.pda), so a known audio
 * extension in the requested path is stripped first. */
static Mix_Chunk *load_audio_chunk(const char *path) {
    char base[1024];
    snprintf(base, sizeof(base), "%s", path);
    char *dot = strrchr(base, '.');
    if (dot && (strcmp(dot, ".wav") == 0 || strcmp(dot, ".mp3") == 0 ||
                strcmp(dot, ".m4a") == 0 || strcmp(dot, ".pda") == 0 ||
                strcmp(dot, ".aif") == 0 || strcmp(dot, ".aiff") == 0))
        *dot = 0;

    const char *dir = g_pd.pdx_dir ? g_pd.pdx_dir : ".";
    char full[1200];
    snprintf(full, sizeof(full), "%s/%s.pda", dir, base);
    if (access(full, F_OK) != 0) pd_fix_path_case(full); /* device FS is case-insensitive */
    Mix_Chunk *c = load_pda_chunk(full);
    if (c) return c;
    snprintf(full, sizeof(full), "%s/%s.wav", dir, base);
    if (access(full, F_OK) != 0) pd_fix_path_case(full);
    c = Mix_LoadWAV(full);
    if (c) return c;
    snprintf(full, sizeof(full), "%s/%s", dir, path);
    if (access(full, F_OK) != 0) pd_fix_path_case(full);
    c = load_pda_chunk(full);
    if (c) return c;
    c = Mix_LoadWAV(full);
    if (c) return c;
    /* legacy: relative to cwd */
    snprintf(full, sizeof(full), "%s.wav", base);
    c = Mix_LoadWAV(full);
    if (c) return c;
    return Mix_LoadWAV(path);
}

static int l_sample_gc2(lua_State *L) {
    (void)L;
    return 0;
}
static int l_ret_zero(lua_State *L) { lua_pushnumber(L, 0); return 1; }
static int l_ret_one(lua_State *L) { lua_pushnumber(L, 1); return 1; }
static int l_ret_false(lua_State *L) { lua_pushboolean(L, 0); return 1; }
static int l_ret_emptytable(lua_State *L) { lua_newtable(L); return 1; }

static int l_sample_new(lua_State *L) {
    /* sample.new(seconds, [format]) allocates an empty buffer; only a
       string argument is a path (Lua coerces numbers, so check the type) */
    const char *path = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : NULL;
    PdSample *s = (PdSample *)lua_newuserdatauv(L, sizeof(PdSample), 0);
    s->chunk = NULL;
    if (path) {
        ensure_audio();
        if (audio_ready > 0) {
            s->chunk = load_audio_chunk(path);
            if (!s->chunk)
                fprintf(stderr, "[sound] could not load sample %s\n", path);
        }
    }
    luaL_setmetatable(L, META_SAMPLE);
    return 1;
}

static int l_player_gc(lua_State *L) {
    (void)L;
    return 0;
}
static int l_noop(lua_State *L) { (void)L; return 0; }

/* bytes per second of chunk data in the opened device format */
static double device_bytes_per_sec(void) {
    int freq = 44100, chans = 2;
    Uint16 fmt = AUDIO_S16LSB;
    if (audio_ready > 0) Mix_QuerySpec(&freq, &fmt, &chans);
    return (double)freq * chans * ((SDL_AUDIO_BITSIZE(fmt)) / 8);
}

static int l_sample_getLength(lua_State *L) {
    PdSample *s = (PdSample *)luaL_checkudata(L, 1, META_SAMPLE);
    if (s->chunk) lua_pushnumber(L, (lua_Number)s->chunk->alen / device_bytes_per_sec());
    else lua_pushnumber(L, 0);
    return 1;
}

static int l_sample_getSampleRate(lua_State *L) {
    (void)L;
    int freq = 44100, chans = 2;
    Uint16 fmt = AUDIO_S16LSB;
    if (audio_ready > 0) Mix_QuerySpec(&freq, &fmt, &chans);
    lua_pushinteger(L, freq);
    return 1;
}

/* sample:getSubsample(startFrame, endFrame) -> new sample.
   Frame offsets are interpreted at the device rate (chunks are stored
   converted to the device format). */
static int l_sample_getSubsample(lua_State *L) {
    PdSample *s = (PdSample *)luaL_checkudata(L, 1, META_SAMPLE);
    lua_Number fa = luaL_optnumber(L, 2, 0);
    lua_Number fb = luaL_optnumber(L, 3, 0);
    PdSample *ns = (PdSample *)lua_newuserdatauv(L, sizeof(PdSample), 0);
    ns->chunk = NULL;
    if (s->chunk && s->chunk->abuf) {
        int freq = 44100, chans = 2;
        Uint16 fmt = AUDIO_S16LSB;
        if (audio_ready > 0) Mix_QuerySpec(&freq, &fmt, &chans);
        long frame_bytes = chans * (SDL_AUDIO_BITSIZE(fmt) / 8);
        long start = (long)fa * frame_bytes;
        long end = (long)fb * frame_bytes;
        long alen = (long)s->chunk->alen;
        if (start < 0) start = 0;
        if (end <= 0 || end > alen) end = alen;
        if (start < end) {
            long len = end - start;
            uint8_t *buf = malloc((size_t)len);
            if (buf) {
                memcpy(buf, s->chunk->abuf + start, (size_t)len);
                ns->chunk = Mix_QuickLoad_RAW(buf, (Uint32)len);
                /* buf intentionally kept alive for the chunk's lifetime */
            }
        }
    }
    luaL_setmetatable(L, META_SAMPLE);
    return 1;
}

static int l_sample_play(lua_State *L) {
    PdSample *s = (PdSample *)luaL_checkudata(L, 1, META_SAMPLE);
    if (audio_ready > 0 && s->chunk)
        Mix_PlayChannel(-1, s->chunk, 0);
    lua_pushboolean(L, s->chunk != NULL);
    return 1;
}

static const luaL_Reg sample_methods[] = {
    {"getLength", l_sample_getLength},
    {"getSampleRate", l_sample_getSampleRate},
    {"getSubsample", l_sample_getSubsample},
    {"play", l_sample_play},
    {"playAt", l_sample_play},
    {"load", l_noop},
    {"save", l_noop},
    {"__gc", l_noop},
    {NULL, NULL}
};

static PdPlayer *check_player(lua_State *L, const char *meta) {
    return (PdPlayer *)luaL_checkudata(L, 1, meta);
}

static int l_player_play_common(lua_State *L, PdPlayer *p, const char *meta) {
    (void)meta;
    /* Playdate: play(0) loops forever, play(n) plays n times, default 1 */
    int rep = (int)luaL_optinteger(L, 2, 1);
    /* play([repeat], [rate]) */
    if (lua_isnumber(L, 3)) p->rate = (float)lua_tonumber(L, 3);
    int loops = rep == 0 ? -1 : rep - 1;
    if (audio_ready > 0 && p->chunk) {
        if (p->paused && p->channel >= 0) {
            Mix_Resume(p->channel);
            p->start_ticks += (uint32_t)pd_time_milliseconds() - p->paused_ticks;
            p->paused = 0;
            p->playing = 1;
            return 0;
        }
        p->channel = Mix_PlayChannel(-1, player_effective_chunk(p), loops);
        if (p->channel >= 0) {
            float volume=fmaxf(p->volume,p->right_volume);
            Mix_Volume(p->channel, (int)(volume * MIX_MAX_VOLUME));
            Mix_SetPanning(p->channel,volume>0 ? (Uint8)(255*p->volume/volume) : 0,volume>0 ? (Uint8)(255*p->right_volume/volume) : 0);
        }
        p->playing = (p->channel >= 0);
        p->start_ticks = (uint32_t)pd_time_milliseconds();
    } else if (audio_ready > 0 && p->music) {
        p->playing = (Mix_PlayMusic(p->music, rep == 0 ? -1 : rep) == 0);
        p->start_ticks = (uint32_t)pd_time_milliseconds();
    }
    return 0;
}

static int l_player_play(lua_State *L) {
    return l_player_play_common(L, check_player(L, META_SAMPLEPLAYER), META_SAMPLEPLAYER);
}

static int l_player_copy(lua_State *L) {
    PdPlayer *p = check_player(L, META_SAMPLEPLAYER);
    PdPlayer *c = (PdPlayer *)lua_newuserdatauv(L, sizeof(PdPlayer), 0);
    *c = *p;
    c->channel = -1;
    c->playing = 0;
    c->paused = 0;
    c->rate_chunk = NULL; /* don't share the rate cache (double free) */
    c->rate_src = NULL;
    luaL_setmetatable(L, META_SAMPLEPLAYER);
    return 1;
}

/* playAt(when, [vol, rightvol, rate]): scheduled playback approximated as
   immediate play; returns true like the SDK on success */
static int l_player_playat(lua_State *L) {
    PdPlayer *p = check_player(L, META_SAMPLEPLAYER);
    lua_settop(L, 1);
    lua_pushinteger(L, 1);
    l_player_play_common(L, p, META_SAMPLEPLAYER);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_fileplayer_play(lua_State *L) {
    return l_player_play_common(L, check_player(L, META_FILEPLAYER), META_FILEPLAYER);
}

static int l_player_stop(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_SAMPLEPLAYER);
    if (p->channel >= 0) Mix_HaltChannel(p->channel);
    p->playing = 0;
    return 0;
}

static int l_fileplayer_stop(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_FILEPLAYER);
    if (p->music) Mix_HaltMusic();
    if (p->chunk && p->channel >= 0) Mix_HaltChannel(p->channel);
    p->playing = 0;
    p->paused = 0;
    return 0;
}

static int l_fileplayer_pause(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_FILEPLAYER);
    if (p->music) Mix_PauseMusic();
    if (p->chunk && p->channel >= 0) {
        Mix_Pause(p->channel);
        p->paused = 1;
    }
    p->paused_ticks = (uint32_t)pd_time_milliseconds();
    p->playing = 0;
    return 0;
}

/* getOffset(): elapsed playback seconds (wall-clock approximation) */
static int l_player_getoffset(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_touserdata(L, 1);
    if (!p || (!p->playing && !p->paused)) { lua_pushnumber(L, 0); return 1; }
    uint32_t end = p->paused ? p->paused_ticks : (uint32_t)pd_time_milliseconds();
    lua_pushnumber(L, (lua_Number)(end - p->start_ticks) / 1000.0);
    return 1;
}

static int l_player_setoffset(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_touserdata(L, 1);
    float sec = (float)luaL_optnumber(L, 2, 0);
    if (p) p->start_ticks = (uint32_t)pd_time_milliseconds() - (uint32_t)(sec * 1000.0f);
    return 0;
}

static int l_player_getlength(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_touserdata(L, 1);
    if (p && p->chunk)
        lua_pushnumber(L, (lua_Number)p->chunk->alen / (44100.0 * 2 * 2));
    else
        lua_pushnumber(L, 3600); /* music length unknown pre SDL_mixer 2.6 */
    return 1;
}

/* A channel can be recycled for another sound after ours finishes; verify
   the chunk on the channel is still ours or games wait forever on
   isPlaying(). */
static int player_channel_active(PdPlayer *p) {
    if (!(p->playing && p->channel >= 0 && Mix_Playing(p->channel))) return 0;
    Mix_Chunk *on_channel = Mix_GetChunk(p->channel);
    return on_channel == p->chunk || (p->rate_chunk && on_channel == p->rate_chunk);
}

static int l_player_isplaying(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_SAMPLEPLAYER);
    lua_pushboolean(L, player_channel_active(p));
    return 1;
}

static int l_fileplayer_isplaying(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_FILEPLAYER);
    if (p->chunk)
        lua_pushboolean(L, player_channel_active(p));
    else
        lua_pushboolean(L, p->playing && Mix_PlayingMusic());
    return 1;
}

static int l_player_setvolume(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_SAMPLEPLAYER);
    p->volume = (float)luaL_optnumber(L, 2, 1.0);
    p->right_volume=(float)luaL_optnumber(L,3,p->volume);
    pd_sound_native_volume(L,1,p->volume,p->right_volume);
    return 0;
}

static int l_fileplayer_setvolume(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_FILEPLAYER);
    p->volume = (float)luaL_optnumber(L, 2, 1.0);
    p->right_volume=(float)luaL_optnumber(L,3,p->volume);
    if (audio_ready > 0) {
        if (p->chunk && p->channel >= 0)
            Mix_Volume(p->channel, (int)(p->volume * MIX_MAX_VOLUME));
        else if (p->music)
            Mix_VolumeMusic((int)(p->volume * MIX_MAX_VOLUME));
    }
    return 0;
}

static int l_player_setrate(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_touserdata(L, 1);
    if (p) p->rate = (float)luaL_optnumber(L, 2, 1.0);
    return 0;
}

static int l_player_getrate(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_touserdata(L, 1);
    lua_pushnumber(L, p && p->rate > 0 ? p->rate : 1.0);
    return 1;
}

/* shared by sampleplayer and fileplayer metatables */
static int l_player_getvolume(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_touserdata(L, 1);
    lua_Number v = p ? p->volume : 1.0;
    lua_pushnumber(L, v);
    lua_pushnumber(L, p ? p->right_volume : v);
    return 2;
}

static int l_player_gc_2(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_SAMPLEPLAYER);
    if (p->chunk && p->channel >= 0) Mix_HaltChannel(p->channel);
    return 0;
}

static int l_fileplayer_gc(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_FILEPLAYER);
    if (p->music) { Mix_HaltMusic(); Mix_FreeMusic(p->music); }
    p->music = NULL;
    return 0;
}

static int l_sampleplayer_setSample(lua_State *L);

static const luaL_Reg sampleplayer_methods[] = {
    {"play", l_player_play},
    {"setSample", l_sampleplayer_setSample},
    {"stop", l_player_stop},
    {"isPlaying", l_player_isplaying},
    {"setVolume", l_player_setvolume},
    {"getVolume", l_player_getvolume},
    {"setRate", l_player_setrate},
    {"getRate", l_player_getrate},
    {"setOffset", l_player_setoffset},
    {"getOffset", l_player_getoffset},
    {"getLength", l_player_getlength},
    {"setLoopRange", l_noop},
    {"setLoopCallback", l_noop},
    {"setStopOnUnderrun", l_noop},
    {"setFinishCallback", l_noop},
    {"setPan", l_noop},
    {"playAt", l_player_playat},
    {"load", l_noop},
    {"setVolumeMod", l_noop},
    {"setRateMod", l_noop},
    {"didUnderrun", l_ret_false},
    {"copy", l_player_copy},
    {"__gc", l_noop},
    {NULL, NULL}
};

static int l_fileplayer_load(lua_State *L);

static const luaL_Reg fileplayer_methods[] = {
    {"play", l_fileplayer_play},
    {"stop", l_fileplayer_stop},
    {"pause", l_fileplayer_pause},
    {"isPlaying", l_fileplayer_isplaying},
    {"setVolume", l_fileplayer_setvolume},
    {"getVolume", l_player_getvolume},
    {"setRate", l_noop},
    {"getRate", l_ret_one},
    {"setOffset", l_player_setoffset},
    {"getOffset", l_player_getoffset},
    {"getLength", l_player_getlength},
    {"setLoopRange", l_noop},
    {"setStopOnUnderrun", l_noop},
    {"setFinishCallback", l_noop},
    {"setVolumeMod", l_noop},
    {"setRateMod", l_noop},
    {"didUnderrun", l_ret_false},
    {"load", l_fileplayer_load},
    {"setBufferSize", l_noop},
    {"__gc", l_noop},
    {NULL, NULL}
};

static int l_sampleplayer_new(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_newuserdatauv(L, sizeof(PdPlayer), 0);
    memset(p, 0, sizeof(*p));
    p->channel = -1;
    p->volume = 1.0f;
    p->right_volume = 1.0f;
    p->rate = 1.0f;
    if (lua_type(L, 1) == LUA_TSTRING) {
        ensure_audio();
        if (audio_ready > 0) {
            const char *path = lua_tostring(L, 1);
            p->chunk = load_audio_chunk(path);
            if (!p->chunk)
                fprintf(stderr, "[sound] could not load %s\n", path);
        }
    } else if (lua_gettop(L) >= 1 && lua_isuserdata(L, 1)) {
        /* sampleplayer.new(sample) */
        PdSample *s = (PdSample *)luaL_testudata(L, 1, META_SAMPLE);
        if (s) {
            ensure_audio();
            p->chunk = s->chunk;
        }
    }
    luaL_setmetatable(L, META_SAMPLEPLAYER);
    return 1;
}

static int l_sampleplayer_setSample(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_SAMPLEPLAYER);
    PdSample *s = (PdSample *)luaL_checkudata(L, 2, META_SAMPLE);
    p->chunk = s->chunk;
    p->rate_src = NULL; /* invalidate rate cache */
    return 0;
}

static void fileplayer_load_path(PdPlayer *p, const char *path) {
    ensure_audio();
    if (audio_ready <= 0) return;
    char full[1200];
    snprintf(full, sizeof(full), "%s/%s.wav", g_pd.pdx_dir ? g_pd.pdx_dir : ".", path);
    if (access(full, F_OK) != 0) pd_fix_path_case(full);
    p->music = Mix_LoadMUS(full);
    if (!p->music) {
        snprintf(full, sizeof(full), "%s/%s.mp3", g_pd.pdx_dir ? g_pd.pdx_dir : ".", path);
        if (access(full, F_OK) != 0) pd_fix_path_case(full);
        p->music = Mix_LoadMUS(full);
    }
    if (!p->music) p->chunk = load_audio_chunk(path);
    if (!p->music && !p->chunk)
        fprintf(stderr, "[sound] could not load %s\n", path);
}

static int l_fileplayer_new(lua_State *L) {
    PdPlayer *p = (PdPlayer *)lua_newuserdatauv(L, sizeof(PdPlayer), 0);
    memset(p, 0, sizeof(*p));
    p->channel = -1;
    p->volume = 1.0f;
    p->right_volume = 1.0f;
    /* fileplayer.new(bufferSeconds) creates an empty player (load later);
       only a real string argument is a path */
    if (lua_type(L, 1) == LUA_TSTRING)
        fileplayer_load_path(p, lua_tostring(L, 1));
    luaL_setmetatable(L, META_FILEPLAYER);
    return 1;
}

static int l_fileplayer_load(lua_State *L) {
    PdPlayer *p = (PdPlayer *)luaL_checkudata(L, 1, META_FILEPLAYER);
    if (lua_type(L, 2) == LUA_TSTRING) {
        p->music = NULL;
        p->chunk = NULL;
        p->rate_src = NULL;
        fileplayer_load_path(p, lua_tostring(L, 2));
    }
    lua_pushboolean(L, p->music != NULL || p->chunk != NULL);
    return 1;
}

static int l_synth_playNote(lua_State *L);
static int l_synth_playMIDINote(lua_State *L);
static int l_synth_stop(lua_State *L);
static int l_synth_setVolume(lua_State *L);
static int l_synth_getVolume(lua_State *L);
static int l_synth_copy(lua_State *L);
static int l_synth_setSample(lua_State *L);

static const luaL_Reg synth_methods[] = {
    {"playNote", l_synth_playNote},
    {"playMIDINote", l_synth_playMIDINote},
    {"stop", l_synth_stop},
    {"noteOff", l_noop},
    {"isPlaying", l_ret_false},
    {"setVolume", l_synth_setVolume},
    {"getVolume", l_synth_getVolume},
    {"copy", l_synth_copy},
    {"setWaveform", l_noop},
    {"setAttack", l_noop},
    {"setDecay", l_noop},
    {"setSustain", l_noop},
    {"setRelease", l_noop},
    {"setADSR", l_noop},
    {"setFrequencyMod", l_noop},
    {"setAmplitudeMod", l_noop},
    {"setVolumeMod", l_noop},
    {"setSample", l_synth_setSample},
    {"setParameter", l_noop},
    {"setLegato", l_noop},
    {"__gc", l_noop},
    {NULL, NULL}
};

static const luaL_Reg channel_methods[] = {
    {"addEffect", l_noop},
    {"removeEffect", l_noop},
    {"addSource", l_noop},
    {"removeSource", l_noop},
    {"setVolume", l_noop},
    {"getVolume", l_ret_one},
    {"setPan", l_noop},
    {"getPan", l_ret_zero},
    {"setDry", l_noop},
    {"setWet", l_noop},
    {"setVolumeMod", l_noop},
    {"setPanMod", l_noop},
    {"remove", l_noop},
    {"__gc", l_noop},
    {NULL, NULL}
};

/* ---- synth (sample-based playback; waveform synthesis not supported) ----
   A sample synth plays its sample at the recorded rate for middle C
   (MIDI 60) and pitch-shifts by resampling for other notes. */
#define SYNTH_CACHE 48
typedef struct {
    Mix_Chunk *chunk;
    float volume;
    struct { float rate; Mix_Chunk *chunk; } cache[SYNTH_CACHE];
    int cache_n;
    int last_channel;
} PdSynth;

static int l_synth_new(lua_State *L) {
    PdSynth *sy = (PdSynth *)lua_newuserdatauv(L, sizeof(PdSynth), 0);
    memset(sy, 0, sizeof(*sy));
    sy->volume = 1.0f;
    sy->last_channel = -1;
    if (lua_gettop(L) >= 2) {
        PdSample *s = (PdSample *)luaL_testudata(L, 1, META_SAMPLE);
        if (s) sy->chunk = s->chunk;
    }
    luaL_setmetatable(L, META_SYNTH);
    return 1;
}

/* "C4", "A#3", "Db5" -> MIDI number; returns -1 on parse failure */
static int note_name_to_midi(const char *s) {
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; /* A..G */
    if (!s || !s[0]) return -1;
    char c = s[0];
    if (c >= 'a' && c <= 'g') c -= 32;
    if (c < 'A' || c > 'G') return -1;
    int semi = base[c - 'A'];
    int i = 1;
    if (s[i] == '#') { semi++; i++; }
    else if (s[i] == 'b') { semi--; i++; }
    int neg = 0;
    if (s[i] == '-') { neg = 1; i++; }
    if (s[i] < '0' || s[i] > '9') return -1;
    int oct = 0;
    while (s[i] >= '0' && s[i] <= '9') oct = oct * 10 + (s[i++] - '0');
    if (neg) oct = -oct;
    return 12 * (oct + 1) + semi;
}

static void synth_play_rate(PdSynth *sy, float rate, float vol) {
    if (!sy->chunk || audio_ready <= 0 || rate <= 0) return;
    Mix_Chunk *ck = NULL;
    if (rate > 0.999f && rate < 1.001f) {
        ck = sy->chunk;
    } else {
        for (int i = 0; i < sy->cache_n; i++)
            if (fabsf(sy->cache[i].rate - rate) < 0.0005f) { ck = sy->cache[i].chunk; break; }
        if (!ck) {
            ck = resample_chunk(sy->chunk, rate);
            if (ck && sy->cache_n < SYNTH_CACHE) {
                sy->cache[sy->cache_n].rate = rate;
                sy->cache[sy->cache_n].chunk = ck;
                sy->cache_n++;
            }
        }
    }
    if (!ck) return;
    if (getenv("PD_SND_LOG"))
        fprintf(stderr, "[synth note rate=%.3f vol=%.2f]\n", rate, vol);
    int ch = Mix_PlayChannel(-1, ck, 0);
    if (ch >= 0) {
        float v = vol * sy->volume;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        Mix_Volume(ch, (int)(v * MIX_MAX_VOLUME));
        sy->last_channel = ch;
    }
}

static float midi_to_rate(float midi) {
    return powf(2.0f, (midi - 60.0f) / 12.0f);
}

/* synth:playNote(pitch, [volume, length, when]) — pitch in Hz or "C4" */
static int l_synth_playNote(lua_State *L) {
    PdSynth *sy = (PdSynth *)luaL_checkudata(L, 1, META_SYNTH);
    float rate = 1.0f;
    if (lua_type(L, 2) == LUA_TSTRING) {
        int m = note_name_to_midi(lua_tostring(L, 2));
        if (m >= 0) rate = midi_to_rate((float)m);
    } else if (lua_isnumber(L, 2)) {
        rate = (float)lua_tonumber(L, 2) / 261.6256f; /* C4 reference */
    }
    float vol = (float)luaL_optnumber(L, 3, 1.0);
    synth_play_rate(sy, rate, vol);
    lua_pushboolean(L, sy->chunk != NULL);
    return 1;
}

static int l_synth_playMIDINote(lua_State *L) {
    PdSynth *sy = (PdSynth *)luaL_checkudata(L, 1, META_SYNTH);
    float midi = 60.0f;
    if (lua_type(L, 2) == LUA_TSTRING) {
        int m = note_name_to_midi(lua_tostring(L, 2));
        if (m >= 0) midi = (float)m;
    } else if (lua_isnumber(L, 2)) {
        midi = (float)lua_tonumber(L, 2);
    }
    float vol = (float)luaL_optnumber(L, 3, 1.0);
    synth_play_rate(sy, midi_to_rate(midi), vol);
    lua_pushboolean(L, sy->chunk != NULL);
    return 1;
}

static int l_synth_setVolume(lua_State *L) {
    PdSynth *sy = (PdSynth *)luaL_checkudata(L, 1, META_SYNTH);
    sy->volume = (float)luaL_optnumber(L, 2, 1.0);
    return 0;
}

static int l_synth_getVolume(lua_State *L) {
    PdSynth *sy = (PdSynth *)luaL_checkudata(L, 1, META_SYNTH);
    lua_pushnumber(L, sy->volume);
    lua_pushnumber(L, sy->volume);
    return 2;
}

static int l_synth_setSample(lua_State *L) {
    PdSynth *sy = (PdSynth *)luaL_checkudata(L, 1, META_SYNTH);
    PdSample *s = (PdSample *)luaL_testudata(L, 2, META_SAMPLE);
    if (s) {
        sy->chunk = s->chunk;
        for (int i = 0; i < sy->cache_n; i++) {
            free(sy->cache[i].chunk->abuf);
            Mix_FreeChunk(sy->cache[i].chunk);
        }
        sy->cache_n = 0;
    }
    return 0;
}

static int l_synth_stop(lua_State *L) {
    PdSynth *sy = (PdSynth *)luaL_checkudata(L, 1, META_SYNTH);
    if (sy->last_channel >= 0) Mix_HaltChannel(sy->last_channel);
    return 0;
}

static int l_synth_copy(lua_State *L) {
    PdSynth *sy = (PdSynth *)luaL_checkudata(L, 1, META_SYNTH);
    PdSynth *c = (PdSynth *)lua_newuserdatauv(L, sizeof(PdSynth), 0);
    memset(c, 0, sizeof(*c));
    c->chunk = sy->chunk;
    c->volume = sy->volume;
    c->last_channel = -1;
    luaL_setmetatable(L, META_SYNTH);
    return 1;
}

/* ---- sequence / track (game-driven stepping via goToStep) ---- */
#define META_SEQUENCE "pd.sequence"
#define META_TRACK "pd.soundtrack"

typedef struct {
    int playing;
    float tempo;
    float step;
} PdSeq;

static int l_track_new(lua_State *L) {
    lua_newuserdatauv(L, 1, 2); /* uv1 = notes table, uv2 = instrument */
    luaL_setmetatable(L, META_TRACK);
    lua_newtable(L);
    lua_setiuservalue(L, -2, 1);
    return 1;
}

/* Count the tracks in a standard MIDI file (MThd header, bytes 10-11). */
static int midi_track_count(const char *path) {
    char full[1200];
    snprintf(full, sizeof(full), "%s/%s", g_pd.pdx_dir ? g_pd.pdx_dir : ".", path);
    if (access(full, F_OK) != 0) pd_fix_path_case(full);
    FILE *f = fopen(full, "rb");
    if (!f) {
        snprintf(full, sizeof(full), "%s/%s.mid", g_pd.pdx_dir ? g_pd.pdx_dir : ".", path);
        if (access(full, F_OK) != 0) pd_fix_path_case(full);
        f = fopen(full, "rb");
    }
    if (!f) return 0;
    uint8_t hdr[12];
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (n < 12 || memcmp(hdr, "MThd", 4) != 0) return 0;
    return (hdr[10] << 8) | hdr[11];
}

static int l_track_setNotes(lua_State *L) {
    luaL_checkudata(L, 1, META_TRACK);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushvalue(L, 2);
    lua_setiuservalue(L, 1, 1);
    return 0;
}

static int l_track_getNotes(lua_State *L) {
    luaL_checkudata(L, 1, META_TRACK);
    lua_getiuservalue(L, 1, 1);
    return 1;
}

static int l_track_addNote(lua_State *L) {
    luaL_checkudata(L, 1, META_TRACK);
    lua_getiuservalue(L, 1, 1);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setiuservalue(L, 1, 1);
    }
    lua_Integer n = lua_rawlen(L, -1);
    if (lua_istable(L, 2)) {
        lua_pushvalue(L, 2);
    } else {
        lua_newtable(L);
        lua_pushvalue(L, 2); lua_setfield(L, -2, "step");
        lua_pushvalue(L, 3); lua_setfield(L, -2, "note");
        if (!lua_isnoneornil(L, 4)) { lua_pushvalue(L, 4); lua_setfield(L, -2, "length"); }
        if (!lua_isnoneornil(L, 5)) { lua_pushvalue(L, 5); lua_setfield(L, -2, "velocity"); }
    }
    lua_rawseti(L, -2, n + 1);
    return 0;
}

static int l_track_setInstrument(lua_State *L) {
    luaL_checkudata(L, 1, META_TRACK);
    lua_pushvalue(L, 2);
    lua_setiuservalue(L, 1, 2);
    return 0;
}

static int l_track_getInstrument(lua_State *L) {
    luaL_checkudata(L, 1, META_TRACK);
    lua_getiuservalue(L, 1, 2);
    return 1;
}

static const luaL_Reg track_methods[] = {
    {"setNotes", l_track_setNotes},
    {"getNotes", l_track_getNotes},
    {"addNote", l_track_addNote},
    {"setInstrument", l_track_setInstrument},
    {"getInstrument", l_track_getInstrument},
    {"setMuted", l_noop},
    {"clearNotes", l_noop},
    {"getLength", l_ret_zero},
    {"getNotesActive", l_ret_zero},
    {"getPolyphony", l_ret_one},
    {"removeNote", l_noop},
    {"__gc", l_noop},
    {NULL, NULL}
};

static int l_sequence_new(lua_State *L) {
    int ntracks = 0;
    if (lua_type(L, 1) == LUA_TSTRING)
        ntracks = midi_track_count(lua_tostring(L, 1));
    PdSeq *sq = (PdSeq *)lua_newuserdatauv(L, sizeof(PdSeq), 1);
    memset(sq, 0, sizeof(*sq));
    sq->tempo = 120.0f;
    luaL_setmetatable(L, META_SEQUENCE);
    lua_newtable(L); /* tracks */
    /* sequence.new("file.mid"): pre-create one (empty) track per MIDI track
       so getTrackAtIndex() returns real track objects games can configure */
    for (int i = 1; i <= ntracks; i++) {
        lua_newuserdatauv(L, 1, 2);
        luaL_setmetatable(L, META_TRACK);
        lua_newtable(L);
        lua_setiuservalue(L, -2, 1);
        lua_rawseti(L, -2, i);
    }
    lua_setiuservalue(L, -2, 1);
    return 1;
}

static int l_sequence_addTrack(lua_State *L) {
    luaL_checkudata(L, 1, META_SEQUENCE);
    int has_arg = luaL_testudata(L, 2, META_TRACK) != NULL;
    lua_getiuservalue(L, 1, 1);
    int tracks = lua_gettop(L);
    lua_Integer n = lua_rawlen(L, tracks);
    if (has_arg)
        lua_pushvalue(L, 2);
    else
        l_track_new(L);
    lua_pushvalue(L, -1);
    lua_rawseti(L, tracks, n + 1);
    return 1;
}

static int l_sequence_getTrackCount(lua_State *L) {
    luaL_checkudata(L, 1, META_SEQUENCE);
    lua_getiuservalue(L, 1, 1);
    lua_pushinteger(L, (lua_Integer)lua_rawlen(L, -1));
    return 1;
}

static int l_sequence_getTrackAtIndex(lua_State *L) {
    luaL_checkudata(L, 1, META_SEQUENCE);
    lua_Integer i = luaL_checkinteger(L, 2);
    lua_getiuservalue(L, 1, 1);
    lua_rawgeti(L, -1, i);
    return 1;
}

static int l_sequence_setTrackAtIndex(lua_State *L) {
    luaL_checkudata(L, 1, META_SEQUENCE);
    lua_Integer i = luaL_checkinteger(L, 2);
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 3);
    lua_rawseti(L, -2, i);
    return 0;
}

static int l_sequence_setTempo(lua_State *L) {
    PdSeq *sq = (PdSeq *)luaL_checkudata(L, 1, META_SEQUENCE);
    sq->tempo = (float)luaL_optnumber(L, 2, 120.0);
    return 0;
}

static int l_sequence_getTempo(lua_State *L) {
    PdSeq *sq = (PdSeq *)luaL_checkudata(L, 1, META_SEQUENCE);
    lua_pushnumber(L, sq->tempo);
    return 1;
}

static int l_sequence_play(lua_State *L) {
    PdSeq *sq = (PdSeq *)luaL_checkudata(L, 1, META_SEQUENCE);
    sq->playing = 1;
    return 0;
}

static int l_sequence_stop(lua_State *L) {
    PdSeq *sq = (PdSeq *)luaL_checkudata(L, 1, META_SEQUENCE);
    sq->playing = 0;
    return 0;
}

static int l_sequence_isPlaying(lua_State *L) {
    PdSeq *sq = (PdSeq *)luaL_checkudata(L, 1, META_SEQUENCE);
    lua_pushboolean(L, sq->playing);
    return 1;
}

static int l_sequence_getCurrentStep(lua_State *L) {
    PdSeq *sq = (PdSeq *)luaL_checkudata(L, 1, META_SEQUENCE);
    lua_pushnumber(L, sq->step);
    return 1;
}

/* sequence:goToStep(step, [playNotes]): trigger every track note whose
   step matches. The SDK plays notes when playNotes is true; some games
   rely on this as their only note trigger while cranking. */
static int l_sequence_goToStep(lua_State *L) {
    PdSeq *sq = (PdSeq *)luaL_checkudata(L, 1, META_SEQUENCE);
    lua_Number stepf = luaL_optnumber(L, 2, 1);
    int step = (int)stepf;
    int prev = (int)sq->step;
    /* Games drive playback by stepping the playhead; sound the notes at
       each newly reached step (the SDK's engine would do this while the
       sequence runs). Explicit playNotes=true also triggers. */
    int trigger = lua_toboolean(L, 3) || step != prev;
    sq->step = (float)stepf;
    if (!trigger) return 0;
    lua_getiuservalue(L, 1, 1); /* tracks */
    int tracks = lua_gettop(L);
    lua_Integer ntr = lua_rawlen(L, tracks);
    for (lua_Integer t = 1; t <= ntr; t++) {
        lua_rawgeti(L, tracks, t);
        if (!luaL_testudata(L, -1, META_TRACK)) { lua_pop(L, 1); continue; }
        int trk = lua_gettop(L);
        lua_getiuservalue(L, trk, 2); /* instrument */
        PdSynth *sy = (PdSynth *)luaL_testudata(L, -1, META_SYNTH);
        lua_pop(L, 1);
        if (!sy) { lua_pop(L, 1); continue; }
        lua_getiuservalue(L, trk, 1); /* notes */
        if (lua_istable(L, -1)) {
            lua_Integer nn = lua_rawlen(L, -1);
            for (lua_Integer j = 1; j <= nn; j++) {
                lua_rawgeti(L, -1, j);
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "step");
                    int nstep = (int)lua_tonumber(L, -1);
                    lua_pop(L, 1);
                    if (nstep == step) {
                        lua_getfield(L, -1, "note");
                        float midi = 60.0f;
                        if (lua_type(L, -1) == LUA_TSTRING) {
                            int m = note_name_to_midi(lua_tostring(L, -1));
                            if (m >= 0) midi = (float)m;
                        } else if (lua_isnumber(L, -1)) {
                            midi = (float)lua_tonumber(L, -1);
                        }
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "velocity");
                        float vel = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
                        lua_pop(L, 1);
                        synth_play_rate(sy, midi_to_rate(midi), vel);
                    }
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 2); /* notes, track */
    }
    lua_pop(L, 1);
    return 0;
}

static const luaL_Reg sequence_methods[] = {
    {"addTrack", l_sequence_addTrack},
    {"getTrackAtIndex", l_sequence_getTrackAtIndex},
    {"setTrackAtIndex", l_sequence_setTrackAtIndex},
    {"setTempo", l_sequence_setTempo},
    {"getTempo", l_sequence_getTempo},
    {"play", l_sequence_play},
    {"stop", l_sequence_stop},
    {"isPlaying", l_sequence_isPlaying},
    {"goToStep", l_sequence_goToStep},
    {"getCurrentStep", l_sequence_getCurrentStep},
    {"setLoops", l_noop},
    {"getLength", l_ret_zero},
    {"allNotesOff", l_noop},
    {"getTrackCount", l_sequence_getTrackCount},
    {"__gc", l_noop},
    {NULL, NULL}
};

static int l_channel_new(lua_State *L) {
    lua_newuserdatauv(L, 8, 0);
    luaL_setmetatable(L, META_CHANNEL);
    return 1;
}

/* Generic stub object: a table whose missing methods are all no-ops that
   return the object itself (chainable) or benign defaults. */
static int l_stub_method(lua_State *L) {
    lua_pushvalue(L, 1);
    return 1;
}

static int l_stub_zero(lua_State *L) {
    lua_pushinteger(L, 0);
    return 1;
}

static int l_stub_false(lua_State *L) {
    lua_pushboolean(L, 0);
    return 1;
}

static int l_stub_index(lua_State *L) {
    const char *key = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    if (strstr(key, "Length") || strstr(key, "Count") || strstr(key, "Size"))
        lua_pushcfunction(L, l_stub_zero);
    else if (strncmp(key, "is", 2) == 0)
        lua_pushcfunction(L, l_stub_false);
    else
        /* getters included: returning self keeps chains like
           sequence:getTrackAtIndex(i):setInstrument(...) alive */
        lua_pushcfunction(L, l_stub_method);
    return 1;
}

static int l_stub_new(lua_State *L) {
    lua_newtable(L);
    lua_newtable(L);
    lua_pushcfunction(L, l_stub_index);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    return 1;
}

static int l_sound_getCurrentTime(lua_State *L) {
    lua_pushnumber(L, (lua_Number)(uint32_t)pd_time_milliseconds() / 1000.0);
    return 1;
}

static void make_meta(lua_State *L, const char *name, const luaL_Reg *methods) {
    luaL_newmetatable(L, name);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, methods, 0);
    lua_pop(L, 1);
}

/* Set __index on the class table (on top of stack) to its metatable so
   games that do Class:method(instance, ...) or local fn = Class.method
   resolve to the real implementation instead of a stub. */
static void link_class_to_meta(lua_State *L, const char *meta_name) {
    luaL_getmetatable(L, meta_name);
    lua_setmetatable(L, -2);
}

void pd_sound_register(lua_State *L) {
    ensure_audio();

    make_meta(L, META_SAMPLE, sample_methods);
    make_meta(L, META_SAMPLEPLAYER, sampleplayer_methods);
    make_meta(L, META_FILEPLAYER, fileplayer_methods);
    make_meta(L, META_SYNTH, synth_methods);
    make_meta(L, META_CHANNEL, channel_methods);
    make_meta(L, META_SEQUENCE, sequence_methods);
    make_meta(L, META_TRACK, track_methods);

    lua_getglobal(L, "playdate");
    lua_newtable(L);

    lua_pushcfunction(L, l_sound_getCurrentTime);
    lua_setfield(L, -2, "getCurrentTime");
    lua_pushcfunction(L, l_noop);
    lua_setfield(L, -2, "resetTime");
    lua_pushcfunction(L, l_ret_zero);
    lua_setfield(L, -2, "getSampleRate");
    lua_pushcfunction(L, l_ret_emptytable);
    lua_setfield(L, -2, "playingSources");

    {
        static const struct { const char *name; int value; } snd_consts[] = {
            {"kWaveSquare", 0}, {"kWaveTriangle", 1}, {"kWaveSine", 2},
            {"kWaveNoise", 3}, {"kWaveSawtooth", 4}, {"kWavePOPhase", 5},
            {"kWavePODigital", 6}, {"kWavePOVosim", 7},
            {"kLFOSquare", 0}, {"kLFOTriangle", 1}, {"kLFOSine", 2},
            {"kLFOSampleAndHold", 3}, {"kLFOSawtoothUp", 4},
            {"kLFOSawtoothDown", 5},
            {"kFilterLowPass", 0}, {"kFilterHighPass", 1},
            {"kFilterBandPass", 2}, {"kFilterNotch", 3}, {"kFilterPEQ", 4},
            {"kFilterLowShelf", 5}, {"kFilterHighShelf", 6},
            {"kFormat8bitMono", 0}, {"kFormat8bitStereo", 1},
            {"kFormat16bitMono", 2}, {"kFormat16bitStereo", 3},
            {NULL, 0}
        };
        for (int i = 0; snd_consts[i].name; i++) {
            lua_pushinteger(L, snd_consts[i].value);
            lua_setfield(L, -2, snd_consts[i].name);
        }
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_sample_new);
    lua_setfield(L, -2, "new");
    link_class_to_meta(L, META_SAMPLE);
    lua_setfield(L, -2, "sample");

    lua_newtable(L);
    lua_pushcfunction(L, l_sampleplayer_new);
    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, l_sampleplayer_setSample);
    lua_setfield(L, -2, "setSample");
    link_class_to_meta(L, META_SAMPLEPLAYER);
    lua_setfield(L, -2, "sampleplayer");

    lua_newtable(L);
    lua_pushcfunction(L, l_fileplayer_new);
    lua_setfield(L, -2, "new");
    link_class_to_meta(L, META_FILEPLAYER);
    lua_setfield(L, -2, "fileplayer");

    lua_newtable(L);
    lua_pushcfunction(L, l_synth_new);
    lua_setfield(L, -2, "new");
    link_class_to_meta(L, META_SYNTH);
    lua_setfield(L, -2, "synth");

    lua_newtable(L);
    lua_pushcfunction(L, l_channel_new);
    lua_setfield(L, -2, "new");
    link_class_to_meta(L, META_CHANNEL);
    lua_setfield(L, -2, "channel");

    lua_newtable(L);
    lua_setfield(L, -2, "effect");

    lua_newtable(L);
    lua_pushcfunction(L, l_sequence_new);
    lua_setfield(L, -2, "new");
    link_class_to_meta(L, META_SEQUENCE);
    lua_setfield(L, -2, "sequence");

    lua_newtable(L);
    lua_pushcfunction(L, l_track_new);
    lua_setfield(L, -2, "new");
    link_class_to_meta(L, META_TRACK);
    lua_setfield(L, -2, "track");

    /* stubbed music engine objects: enough to not crash */
    static const char *stub_classes[] = {
        "instrument", "lfo", "envelope",
        "bitcrusher", "twopolefilter", "onepolefilter", "ringmod",
        "overdrive", "delayline", "controlsignal", "micinput", NULL
    };
    for (int i = 0; stub_classes[i]; i++) {
        lua_newtable(L);
        lua_pushcfunction(L, l_stub_new);
        lua_setfield(L, -2, "new");
        lua_setfield(L, -2, stub_classes[i]);
    }

    lua_setfield(L, -2, "sound");
    lua_pop(L, 1);
}

int pd_sound_native_loaded(lua_State *L,int index) {
    PdSample *sample=luaL_testudata(L,index,META_SAMPLE);if(sample)return sample->chunk!=NULL;
    PdPlayer *player=luaL_testudata(L,index,META_SAMPLEPLAYER);if(!player)player=luaL_testudata(L,index,META_FILEPLAYER);
    return player && (player->chunk || player->music);
}
void pd_sound_native_volume(lua_State *L,int index,float left,float right) {
    PdPlayer *player=luaL_testudata(L,index,META_SAMPLEPLAYER);if(!player)player=luaL_testudata(L,index,META_FILEPLAYER);if(!player)return;
    player->volume=fminf(1,fmaxf(0,left));player->right_volume=fminf(1,fmaxf(0,right));float max=fmaxf(player->volume,player->right_volume);
    if(player->channel>=0 && player_channel_active(player)){Mix_Volume(player->channel,(int)(max*MIX_MAX_VOLUME));Mix_SetPanning(player->channel,max>0?(Uint8)(255*player->volume/max):0,max>0?(Uint8)(255*player->right_volume/max):0);}
    else if(player->music)Mix_VolumeMusic((int)(max*MIX_MAX_VOLUME));
}
void pd_sound_native_pause(lua_State *L,int index,int paused) {
    PdPlayer *player=luaL_testudata(L,index,META_SAMPLEPLAYER);if(!player)return;
    if(paused && player->playing && !player->paused && player->channel>=0){Mix_Pause(player->channel);player->paused=1;player->paused_ticks=(uint32_t)pd_time_milliseconds();}
    else if(!paused && player->paused && player->channel>=0){Mix_Resume(player->channel);player->start_ticks+=(uint32_t)pd_time_milliseconds()-player->paused_ticks;player->paused=0;}
}
static void free_owned_chunk(Mix_Chunk *chunk){if(!chunk)return;if(!chunk->allocated)free(chunk->abuf);Mix_FreeChunk(chunk);}
void pd_sound_native_dispose(lua_State *L,int index) {
    PdSample *sample=luaL_testudata(L,index,META_SAMPLE);
    if(sample){for(int ch=0;ch<32;ch++)if(Mix_GetChunk(ch)==sample->chunk)Mix_HaltChannel(ch);free_owned_chunk(sample->chunk);sample->chunk=NULL;return;}
    PdPlayer *player=luaL_testudata(L,index,META_SAMPLEPLAYER);int owns=0;
    if(!player){player=luaL_testudata(L,index,META_FILEPLAYER);owns=1;}if(!player)return;
    if(player->channel>=0 && (Mix_GetChunk(player->channel)==player->chunk || Mix_GetChunk(player->channel)==player->rate_chunk))Mix_HaltChannel(player->channel);
    if(player->music){Mix_HaltMusic();Mix_FreeMusic(player->music);}player->music=NULL;
    free_owned_chunk(player->rate_chunk);player->rate_chunk=NULL;
    if(owns)free_owned_chunk(player->chunk);player->chunk=NULL;player->playing=player->paused=0;
}
