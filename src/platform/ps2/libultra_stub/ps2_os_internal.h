/**
 * ps2_os_internal.h — Infraestructura compartida de los stubs de libultra
 *
 * ── POR QUÉ EXISTE ESTE ARCHIVO ────────────────────────────────────────
 *
 * La versión anterior de los stubs redefinía los structs de libultra
 * (OSThread, OSMesgQueue, OSTimer) añadiéndoles campos propios de PS2.
 * Eso es un error grave: el resto del juego ya declara esos objetos con
 * el tamaño ORIGINAL de N64 (OSMesgQueue = 0x18 bytes, OSThread = 0x1B0,
 * OSTimer = 0x20). Si el stub cree que son más grandes, cada memset o
 * escritura pisa la memoria del vecino.
 *
 * Ejemplo real del juego (src/code/main.c):
 *      OSMesgQueue sSerialEventQueue;   // 0x18 bytes
 *      OSMesg      sSerialMsgBuf[1];    // justo detrás
 * Con el struct inflado, osCreateMesgQueue() borraba sSerialMsgBuf.
 *
 * ── SOLUCIÓN ──────────────────────────────────────────────────────────
 *
 * Los structs de libultra se dejan EXACTAMENTE como están. Todo lo
 * específico de PS2 (ids de semáforo, id de hilo EE, pila reservada...)
 * vive en tablas laterales indexadas por el puntero del objeto.
 *
 * Son tablas pequeñas con búsqueda lineal. OoT usa ~8 hilos y unas
 * decenas de colas, así que el coste es irrelevante y a cambio se
 * elimina toda una clase de corrupción de memoria.
 */

#ifndef PS2_OS_INTERNAL_H
#define PS2_OS_INTERNAL_H

#include <kernel.h>
#include <stdio.h>
#include "ps2_types.h"

/* ───────────────────────────────────────────────────────────────── *
 *  Capacidades
 * ───────────────────────────────────────────────────────────────── */
#define PS2_OS_MAX_THREADS    16   /* OoT crea ~8                    */
#define PS2_OS_MAX_QUEUES     64   /* colas de mensajes simultáneas  */
#define PS2_OS_MAX_TIMERS     16   /* temporizadores software        */

/* ───────────────────────────────────────────────────────────────── *
 *  Mapeo de prioridades N64 → EE
 *
 *  libultra: número MAYOR = más prioridad (OoT usa 10..19).
 *  EE kernel: número MENOR = más prioridad (rango válido 1..127).
 *  Por tanto hay que INVERTIR.
 * ───────────────────────────────────────────────────────────────── */
static inline int ps2_os_map_priority(int n64_pri) {
    int ee = 64 - n64_pri;
    if (ee < 1)   ee = 1;
    if (ee > 126) ee = 126;
    return ee;
}

/* ───────────────────────────────────────────────────────────────── *
 *  Sección crítica barata para proteger las tablas laterales.
 *  DI/EI a nivel EE: las tablas se tocan muy poco y muy brevemente.
 * ───────────────────────────────────────────────────────────────── */
static inline int ps2_os_lock(void) {
    return DIntr();
}

static inline void ps2_os_unlock(int prev) {
    if (prev) {
        EIntr();
    }
}

/* ───────────────────────────────────────────────────────────────── *
 *  Notificación de eventos (osSetEventMesg) — implementado en osEvent.c
 *
 *  En N64 las interrupciones del RSP/RDP/VI/SI mandan un mensaje a una
 *  cola registrada con osSetEventMesg. El scheduler de OoT DEPENDE de
 *  esto: manda una tarea gráfica y se queda bloqueado esperando el
 *  mensaje "RSP_DONE". Sin esto, se cuelga en el primer frame.
 * ───────────────────────────────────────────────────────────────── */

/* Valores de OSEvent que nos interesan (ultra64/os.h) */
#define PS2_OS_EVENT_SW1        0
#define PS2_OS_EVENT_SW2        1
#define PS2_OS_EVENT_CART       2
#define PS2_OS_EVENT_COUNTER    3
#define PS2_OS_EVENT_SP         4
#define PS2_OS_EVENT_SI         5
#define PS2_OS_EVENT_AI         6
#define PS2_OS_EVENT_VI         7
#define PS2_OS_EVENT_PI         8
#define PS2_OS_EVENT_DP         9
#define PS2_OS_EVENT_CPU_BREAK  10
#define PS2_OS_EVENT_SP_BREAK   11
#define PS2_OS_EVENT_FAULT      12
#define PS2_OS_EVENT_THREADSTATUS 13
#define PS2_OS_EVENT_PRENMI     14
#define PS2_OS_NUM_EVENTS       15

/**
 * Dispara el evento indicado: manda (sin bloquear) el mensaje que se
 * registró con osSetEventMesg a su cola. Si nadie registró nada, no
 * hace nada. Seguro de llamar desde contexto normal.
 */
void ps2_os_fire_event(int event);

#endif /* PS2_OS_INTERNAL_H */
