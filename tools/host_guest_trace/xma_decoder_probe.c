// Dependency smoke check, not a game-audio decoding test.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "libavcodec/avcodec.h"
#include "libavutil/log.h"

int main(void) {
    av_log_set_level(AV_LOG_QUIET);
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
    assert(codec && strcmp(codec->name, "xmaframes") == 0);
    const int rates[] = {24000, 32000, 44100, 48000};
    unsigned opened = 0, invalid = 0;
    for (unsigned r = 0; r < 4; ++r) {
        for (unsigned channels = 1; channels <= 2; ++channels) {
            AVCodecContext* context = avcodec_alloc_context3(codec);
            AVPacket* packet = av_packet_alloc();
            AVFrame* frame = av_frame_alloc();
            assert(context && packet && frame);
            context->sample_rate = rates[r];
            context->channels = channels;
            assert(avcodec_open2(context, codec, NULL) == 0);
            ++opened;
            assert(av_new_packet(packet, 2) == 0);
            memset(packet->data, 0, packet->size);
            int result = avcodec_send_packet(context, packet);
            if (result >= 0) result = avcodec_receive_frame(context, frame);
            assert(result == AVERROR_INVALIDDATA);
            ++invalid;
            av_frame_free(&frame);
            av_packet_free(&packet);
            avcodec_free_context(&context);
        }
    }
    printf("XMA_ANDROID_CODEC_OPEN_PASS configurations=%u malformed_frames_rejected=%u; no game PCM decoded\n", opened, invalid);
    return 0;
}
