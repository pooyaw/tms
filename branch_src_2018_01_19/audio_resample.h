#ifndef __AUDIO_RESAMPLE_H__
#define __AUDIO_RESAMPLE_H__

extern "C" 
{
#include "libavresample/avresample.h"
}

class AudioResample
{
public:
    AudioResample();
    ~AudioResample();

    int Init(const int& in_channel_layout, const int& out_channel_layout, 
             const int& in_sample_rate, const int& out_sample_rate,
             const int& in_sample_fmt, const int& out_sample_fmt);

    int Resample(const AVFrame* frame, int& got_resample);

    AVFrame* GetResampleFrame()
    {
        return resample_frame_;
    }

    int OpenPcmFd();

private:
    AVAudioResampleContext* resample_ctx_;
    AVFrame* resample_frame_;

	int in_channel_layout_;
    int in_sample_rate_;
    int in_sample_fmt_;
	int out_channel_layout_;
    int out_sample_rate_;
    int out_sample_fmt_;

    int resample_pcm_fd_;
};

#endif // __AUDIO_RESAMPLE_H__