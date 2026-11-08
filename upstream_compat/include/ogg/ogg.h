#pragma once
typedef struct ogg_sync_state { int unused; } ogg_sync_state;
typedef struct ogg_stream_state { int unused; } ogg_stream_state;
typedef struct ogg_page { int unused; } ogg_page;
typedef struct ogg_packet { unsigned char* packet; long bytes; } ogg_packet;
static inline int ogg_sync_init(ogg_sync_state*) { return 0; }
static inline char* ogg_sync_buffer(ogg_sync_state*, long) { return 0; }
static inline int ogg_sync_wrote(ogg_sync_state*, long) { return 0; }
static inline int ogg_sync_pageout(ogg_sync_state*, ogg_page*) { return 0; }
static inline int ogg_stream_init(ogg_stream_state*, int) { return 0; }
static inline int ogg_stream_pagein(ogg_stream_state*, ogg_page*) { return 0; }
static inline int ogg_stream_packetout(ogg_stream_state*, ogg_packet*) { return 0; }
static inline int ogg_stream_clear(ogg_stream_state*) { return 0; }
static inline int ogg_sync_clear(ogg_sync_state*) { return 0; }
static inline int ogg_page_serialno(const ogg_page*) { return 0; }
