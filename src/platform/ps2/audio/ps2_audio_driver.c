/**
 * ps2_audio_driver.c — Audio Driver for OoT PS2 Port
 *
 * OoT's audio system has three components:
 *   1. Audio sequences (MIDI-like, N64 Seq format)
 *   2. Sound effects (ADPCM samples)
 *   3. Audio synthesis tasks (submitted to the RSP)
 *
 * On PS2 we use:
 *   - audsrv (PS2 audio server over IOP) for PCM playback
 *   - A software mixer running on the EE to replace the RSP audio DSP
 *   - SPU2 reverb effects where possible
 *
 * ══════════════════════════════════════════════════════════════════
 *  QUÉ ESTABA MAL
 * ══════════════════════════════════════════════════════════════════
 *
 * 1. COLISIÓN DE SÍMBOLOS CON EL PROPIO JUEGO
 *
 *    Este archivo definía Audio_Init() y func_800F4C58(). Ambos nombres
 *    YA EXISTEN en el decomp:
 *      · Audio_Init()      → src/audio/game/general.c
 *      · func_800F4C58()   → src/audio/game/general.c, y encima con otra
 *                            firma por completo: (Vec3f*, u16, u8)
 *
 *    No saltaba porque el Makefile no compilaba src/audio. En cuanto se
 *    compila (que es lo que hace ahora), el enlazador da "multiple
 *    definition" y el build muere.
 *
 *    Solución: todo lo nuestro lleva prefijo PS2Audio_.
 *
 * 2. audsrv_init() SE LLAMABA DOS VECES (aquí y en ps2_main.c).
 *
 * 3. EL MEZCLADOR AVANZABA MAL LA POSICIÓN
 *
 *    `voice->pos += voice->fracPos >> 16;` con `fracPos &= 0xFFFF`
 *    descarta el acarreo cuando pitchStep supera 1.0 (o sea, cualquier
 *    sonido agudo): el tono salía mal y los samples se alargaban.
 *
 * 4. NO COMPROBABA EL FINAL DEL SAMPLE tras avanzar, sólo al principio
 *    de cada iteración, así que podía leer un sample fuera del buffer.
 *
 * 5. Audio_Update() escribía SIEMPRE el buffer entero aunque audsrv
 *    tuviera menos hueco, cortando el audio a la mitad de un bloque.
 *
 * Arquitectura:
 *   PS2Audio_Init()        — al arrancar
 *   PS2Audio_Update()      — una vez por frame (desde el vblank)
 *   PS2Audio_PlaySfx()     — reproducir un efecto
 *   PS2Audio_ProcessTask() — recibe las tareas de audio del scheduler
 */

#include <audsrv.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "ps2_types.h"

/* ─────────────────────────────────────────────────────────── *
 *  Audio configuration
 *  OoT mixes at 32000 Hz on N64 (RSP output rate).
 *  PS2 audsrv supports: 8000, 11025, 22050, 32000, 44100, 48000 Hz.
 * ─────────────────────────────────────────────────────────── */
#define AUDIO_SAMPLE_RATE    32000
#define AUDIO_CHANNELS       2        /* stereo */
#define AUDIO_BITS           16       /* 16-bit PCM */
#define AUDIO_BUFFER_SAMPLES 1024     /* ~32ms at 32kHz */
#define AUDIO_BUFFER_BYTES   (AUDIO_BUFFER_SAMPLES * AUDIO_CHANNELS * \
                              (AUDIO_BITS / 8))

/* ─────────────────────────────────────────────────────────── *
 *  Software mixer state
 *  We maintain a small set of voices, each playing a sample.
 * ─────────────────────────────────────────────────────────── */
#define MAX_AUDIO_VOICES  16

typedef struct {
    const s16*  samples;     /* pointer to decoded PCM data      */
    u32         numSamples;  /* total sample count               */
    u32         pos;         /* current playback position        */
    s32         pitchStep;   /* resampling step (16.16 fixed)    */
    u32         fracPos;     /* fractional position accumulator  */
    s16         volL, volR;  /* volume 0–0x7FFF                  */
    bool        active;
    bool        loop;
    u32         loopStart;
} AudioVoice;

static AudioVoice  s_voices[MAX_AUDIO_VOICES];
static bool s_audioReady;
static bool s_nativeOutput;
static s16         s_mixBuf[AUDIO_BUFFER_BYTES / sizeof(s16)]
                            __attribute__((aligned(64)));

/* ─────────────────────────────────────────────────────────── *
 *  audsrv format descriptor
 * ─────────────────────────────────────────────────────────── */
static struct audsrv_fmt_t s_audfmt = {
    .freq = AUDIO_SAMPLE_RATE,
    .bits = AUDIO_BITS,
    .channels = AUDIO_CHANNELS,
};

/* ─────────────────────────────────────────────────────────── *
 *  Audio_Init — initialize audsrv and mixer
 * ─────────────────────────────────────────────────────────── */
void PS2Audio_Init(void) {
    int ret;
    s_audioReady = false;
    s_nativeOutput = false;

    /* audsrv_init() ya NO se llama aquí: lo hace ps2_main.c una sola
     * vez, después de cargar audsrv.irx. Llamarla dos veces dejaba el
     * canal RPC en un estado inconsistente. */
    ret = audsrv_init();
    if (ret != 0) {
        printf("[Audio] audsrv_init fallo: %d (seguimos sin sonido)\n", ret);
        return;
    }

    ret = audsrv_set_format(&s_audfmt);
    if (ret != 0) {
        printf("[Audio] audsrv_set_format failed: %d\n", ret);
        return;
    }

    audsrv_set_volume(MAX_VOLUME);

    memset(s_voices, 0, sizeof(s_voices));
    memset(s_mixBuf, 0, sizeof(s_mixBuf));
    s_audioReady = true;

    printf("[Audio] Initialized: %d Hz, %d-bit, %d ch\n",
           AUDIO_SAMPLE_RATE, AUDIO_BITS, AUDIO_CHANNELS);
}

/* ─────────────────────────────────────────────────────────── *
 *  _mix_voices() — software mix all active voices into s_mixBuf
 * ─────────────────────────────────────────────────────────── */
static void _mix_voices(int numSamples) {
    memset(s_mixBuf, 0, numSamples * AUDIO_CHANNELS * sizeof(s16));

    for (int v = 0; v < MAX_AUDIO_VOICES; v++) {
        AudioVoice* voice = &s_voices[v];
        if (!voice->active) continue;

        for (int i = 0; i < numSamples; i++) {
            /* Comprobación al principio Y después de avanzar (abajo):
             * antes sólo estaba aquí y se podía leer fuera del buffer. */
            if (voice->pos >= voice->numSamples) {
                if (voice->loop) {
                    voice->pos = voice->loopStart;
                } else {
                    voice->active = false;
                    break;
                }
            }
            if (!voice->samples) { voice->active = false; break; }

            s16 sample = voice->samples[voice->pos];

            /* Mix into stereo output (clamp to s16 range) */
            s32 outL = (s32)s_mixBuf[i * 2 + 0] +
                       ((s32)sample * voice->volL) / 0x7FFF;
            s32 outR = (s32)s_mixBuf[i * 2 + 1] +
                       ((s32)sample * voice->volR) / 0x7FFF;

            if (outL >  0x7FFF) outL =  0x7FFF;
            if (outL < -0x8000) outL = -0x8000;
            if (outR >  0x7FFF) outR =  0x7FFF;
            if (outR < -0x8000) outR = -0x8000;

            s_mixBuf[i * 2 + 0] = (s16)outL;
            s_mixBuf[i * 2 + 1] = (s16)outR;

            /* Avance con remuestreo.
             * CORREGIDO: el acarreo son los bits por encima de 16, no
             * sólo uno. Con `&= 0xFFFF` se perdía el sobrante en cuanto
             * pitchStep pasaba de 1.0 (cualquier sonido agudo), y el
             * tono salía mal. */
            voice->fracPos += (u32)voice->pitchStep;
            voice->pos     += (voice->fracPos >> 16);
            voice->fracPos &= 0xFFFFu;
        }
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  Audio_Update — called once per frame (from game loop)
 *  Mixes voices and pushes PCM to audsrv.
 * ─────────────────────────────────────────────────────────── */
void PS2Audio_Update(void) {
    if (!s_audioReady || s_nativeOutput) return;

    /* Enviar el PCM mezclado a audsrv.
     * CORREGIDO: sólo escribimos si cabe el bloque ENTERO. Antes se
     * escribía un trozo suelto cuando había poco hueco, lo que parte un
     * frame estéreo por la mitad y desincroniza los canales para
     * siempre (a partir de ahí, izquierda y derecha quedan cruzados). */
    int free_bytes = audsrv_available();
    if (free_bytes >= AUDIO_BUFFER_BYTES) {
        _mix_voices(AUDIO_BUFFER_SAMPLES);
        audsrv_play_audio((char*)s_mixBuf, AUDIO_BUFFER_BYTES);
    }
}

/* The game's audio interface queues native stereo PCM through audsrv. */
s32 osAiSetFrequency(u32 frequency) {
    if (!s_audioReady) return -1;
    struct audsrv_fmt_t format = s_audfmt;
    format.freq = frequency;
    if (audsrv_set_format(&format) != 0) return -1;
    s_audfmt = format;
    s_nativeOutput = true;
    return (s32)frequency;
}

u32 osAiGetLength(void) {
    if (!s_audioReady) return 0;
    int queued = audsrv_queued();
    return queued > 0 ? (u32)queued : 0;
}

int PS2Audio_SubmitBuffer(void* buffer, unsigned int size) {
    if (!s_audioReady || !buffer || (size & 3) || size > 0x40000) return -1;
    if (audsrv_available() < (int)size) return -1;
    s_nativeOutput = true;
    return audsrv_play_audio((const char*)buffer, size) == (int)size ? 0 : -1;
}

/* ─────────────────────────────────────────────────────────── *
 *  Audio_PlaySfx — play a sound effect
 *
 *  @param samples    pointer to 16-bit PCM data (already decoded)
 *  @param numSamples total sample count
 *  @param volL/R     volume 0–0x7FFF
 *  @param pitch      resampling ratio (0x10000 = original pitch)
 *  @param loop       loop the sample?
 *
 *  Returns: voice index, or -1 if no free voice.
 * ─────────────────────────────────────────────────────────── */
int PS2Audio_PlaySfx(const s16* samples, u32 numSamples,
                  s16 volL, s16 volR, u32 pitch, bool loop) {
    for (int i = 0; i < MAX_AUDIO_VOICES; i++) {
        if (!s_voices[i].active) {
            s_voices[i].samples    = samples;
            s_voices[i].numSamples = numSamples;
            s_voices[i].pos        = 0;
            s_voices[i].fracPos    = 0;
            s_voices[i].pitchStep  = (s32)pitch;
            s_voices[i].volL       = volL;
            s_voices[i].volR       = volR;
            s_voices[i].loop       = loop;
            s_voices[i].loopStart  = 0;
            s_voices[i].active     = true;
            return i;
        }
    }
    return -1;  /* all voices busy */
}

/* ─────────────────────────────────────────────────────────── *
 *  Audio_StopVoice
 * ─────────────────────────────────────────────────────────── */
void PS2Audio_StopVoice(int voiceIdx) {
    if (voiceIdx >= 0 && voiceIdx < MAX_AUDIO_VOICES) {
        s_voices[voiceIdx].active = false;
    }
}

/* ─────────────────────────────────────────────────────────── *
 *  PS2Audio_ProcessTask — recibe las tareas de audio del scheduler
 *
 *  En N64 el juego empaqueta comandos de síntesis y se los manda al
 *  RSP. Aquí, de momento, mezclamos por nuestra cuenta en
 *  PS2Audio_Update(), así que sólo aceptamos la tarea y la damos por
 *  terminada (osTask.c ya dispara el evento correspondiente).
 *
 *  El paso siguiente del port sería interpretar de verdad esta lista de
 *  comandos, que es lo que haría sonar la música del juego en vez de
 *  sólo los efectos.
 * ─────────────────────────────────────────────────────────── */
void PS2Audio_ProcessTask(void* cmdList, u32 size) {
    (void)cmdList;
    (void)size;
}

/* NOTA: aquí había dos funciones más, AudioThread_NotifyTaskDone() y
 * func_800F4C58(). Se han eliminado porque AMBAS existen ya en el
 * decomp con otra firma, y definirlas aquí rompe el enlace en cuanto se
 * compila src/audio. Ver la cabecera del archivo. */

/* Frecuencia de una nota MIDI. Con prefijo propio para no chocar con
 * las tablas de audio del juego. */
u32 PS2Audio_GetNoteFrequency(u8 note) {
    /* Standard MIDI frequency table: A4 = 440 Hz */
    static const float BASE_FREQ = 440.0f;
    static const float SEMITONE  = 1.05946309436f;  /* 2^(1/12) */
    int steps = (int)note - 69;  /* MIDI A4 = 69 */
    float freq = BASE_FREQ;
    if (steps > 0) {
        for (int i = 0; i < steps; i++) freq *= SEMITONE;
    } else {
        for (int i = 0; i < -steps; i++) freq /= SEMITONE;
    }
    return (u32)(freq * 256.0f);  /* Q8.8 fixed-point */
}
