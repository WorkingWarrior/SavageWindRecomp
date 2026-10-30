// hwopus: the hardware Opus decoder service, decoded on the host with libopus.
// Packet framing: an 8-byte header {u32 BE payload size, u32 BE final range} before each Opus packet; the consumed
// size includes it and sample counts are per channel. TotK opens it; BOTW's audio path has not been seen to.
#if defined(__linux__) && defined(__has_include)
#if __has_include(<opus/opus.h>)
#include <opus/opus.h>
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

typedef struct { OpusDecoder *dec; int rate, ch; } HwOpus;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static void hwopus_close(Service *s) {
    HwOpus *h = s->state;
    if (h) { opus_decoder_destroy(h->dec); free(h); }
    s->state = NULL;
}

// IHardwareOpusDecoder: 0 DecodeInterleavedOld, 4 ...WithPerfOld, 6 ...WithPerfAndResetOld, 8 DecodeInterleaved
static void decoder_handler(Service *s, IpcReq *req, IpcResp *resp) {
    HwOpus *h = s->state;
    switch (req->cmd) {
    case 0: case 4: case 6: case 8: {
        bool reset = (req->cmd == 6 || req->cmd == 8) && req->data_size >= 1 && IN(uint8_t, 0);
        IpcBuf *in = in_buf(req, 0), *out = out_buf(req, 0);
        if (!in || !out || in->size < 8) FATAL("[hwopus] decode cmd %u without buffers (in %llu)\n", req->cmd, (unsigned long long)(in ? in->size : 0));
        const uint8_t *pkt = (const uint8_t *)(uintptr_t)in->addr;
        uint32_t len = be32(pkt);
        if (8 + (uint64_t)len > in->size) { LOG("[hwopus] truncated packet %u of %llu\n", len, (unsigned long long)in->size); resp->result = 0x1; break; }
        if (reset) opus_decoder_ctl(h->dec, OPUS_RESET_STATE);
        int frames = opus_decode(h->dec, pkt + 8, (opus_int32)len, (opus_int16 *)(uintptr_t)out->addr,
                                 (int)(out->size / (2 * (uint64_t)h->ch)), 0);
        static int logged;
        if (frames < 0) {
            if (logged++ < 8) LOG("[hwopus] decode error %s (packet %u bytes)\n", opus_strerror(frames), len);
            resp->result = 0x1;
            break;
        }
        if (!logged++) LOG("[hwopus] first packet: %u bytes -> %d samples/ch (%d ch)\n", len, frames, h->ch);
        OUT(int32_t, 0) = 8 + (int32_t)len;
        OUT(int32_t, 4) = frames;
        OUT(uint64_t, 8) = 0;
        resp_size(resp, req->cmd == 0 ? 8 : 16);
        break;
    }
    case 1: break;  // SetContext
    default: FATAL("[hwopus] decoder command %u is not implemented\n", req->cmd);
    }
}

// IHardwareOpusDecoderManager: 0/4 open, 1/5 work buffer size (the decoder state lives in libopus)
SERVICE("hwopus", hwopus_manager) {
    (void)s;
    switch (req->cmd) {
    case 0: case 4: {
        int rate = IN(int32_t, 0), ch = IN(int32_t, 4), err = 0;
        OpusDecoder *d = opus_decoder_create(rate, ch, &err);
        LOG("[hwopus] open cmd %u: %d Hz, %d ch (data %u bytes) -> %s\n", req->cmd, rate, ch, req->data_size, d ? "ok" : opus_strerror(err));
        if (!d) { resp->result = 0x1; break; }
        HwOpus *h = calloc(1, sizeof *h);
        *h = (HwOpus){d, rate, ch};
        Service *dec = service_new("IHardwareOpusDecoder", decoder_handler, h);
        dec->on_close = hwopus_close;
        ipc_return_object(resp, dec);
        break;
    }
    case 1: case 5: OUT(uint32_t, 0) = 0x8000; resp_size(resp, 4); break;
    default: FATAL("[hwopus] manager command %u is not implemented\n", req->cmd);
    }
}
#endif
#endif
