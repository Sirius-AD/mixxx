/***************************************************************************
                          sounddevice.cpp
                             -------------------
    begin                : Sun Aug 12, 2007, past my bedtime
    copyright            : (C) 2007 Albert Santoni
    email                : gamegod \a\t users.sf.net
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include <QtDebug>
#include <cstring> // for memcpy and strcmp

#include "sounddevice.h"

#include "soundmanagerutil.h"
#include "soundmanager.h"
#include "util/debug.h"
#include "sampleutil.h"

SoundDevice::SoundDevice(ConfigObject<ConfigValue> * config, SoundManager * sm)
        : m_pConfig(config),
          m_pSoundManager(sm),
          m_strInternalName("Unknown Soundcard"),
          m_strDisplayName("Unknown Soundcard"),
          m_iNumOutputChannels(2),
          m_iNumInputChannels(2),
          m_dSampleRate(44100.0),
          m_hostAPI("Unknown API"),
          m_framesPerBuffer(0),
          m_pDownmixBuffer(SampleUtil::alloc(MAX_BUFFER_LEN)) {
}

SoundDevice::~SoundDevice() {
    SampleUtil::free(m_pDownmixBuffer);
}

QString SoundDevice::getInternalName() const {
    return m_strInternalName;
}

QString SoundDevice::getDisplayName() const {
    return m_strDisplayName;
}

QString SoundDevice::getHostAPI() const {
    return m_hostAPI;
}

int SoundDevice::getNumInputChannels() const {
    return m_iNumInputChannels;
}

int SoundDevice::getNumOutputChannels() const {
    return m_iNumOutputChannels;
}

void SoundDevice::setHostAPI(QString api) {
    m_hostAPI = api;
}

void SoundDevice::setSampleRate(double sampleRate) {
    if (sampleRate <= 0.0) {
        // this is the default value used elsewhere in this file
        sampleRate = 44100.0;
    }
    m_dSampleRate = sampleRate;
}

void SoundDevice::setFramesPerBuffer(unsigned int framesPerBuffer) {
    if (framesPerBuffer * 2 > (unsigned int) MAX_BUFFER_LEN) {
        // framesPerBuffer * 2 because a frame will generally end up
        // being 2 samples and MAX_BUFFER_LEN is a number of samples
        // this isn't checked elsewhere, so...
        reportFatalErrorAndQuit("framesPerBuffer too big in "
                                "SoundDevice::setFramesPerBuffer(uint)");
    }
    m_framesPerBuffer = framesPerBuffer;
}

SoundDeviceError SoundDevice::addOutput(const AudioOutputBuffer &out) {
    //Check if the output channels are already used
    foreach (AudioOutputBuffer myOut, m_audioOutputs) {
        if (out.channelsClash(myOut)) {
            return SOUNDDEVICE_ERROR_DUPLICATE_OUTPUT_CHANNEL;
        }
    }
    if (out.getChannelGroup().getChannelBase()
            + out.getChannelGroup().getChannelCount() > getNumOutputChannels()) {
        return SOUNDDEVICE_ERROR_EXCESSIVE_OUTPUT_CHANNEL;
    }
    m_audioOutputs.append(out);
    return SOUNDDEVICE_ERROR_OK;
}

void SoundDevice::clearOutputs() {
    m_audioOutputs.clear();
}

SoundDeviceError SoundDevice::addInput(const AudioInputBuffer &in) {
    // DON'T check if the input channels are already used, there's no reason
    // we can't send the same inputted samples to different places in mixxx.
    // -- bkgood 20101108
    if (in.getChannelGroup().getChannelBase()
            + in.getChannelGroup().getChannelCount() > getNumInputChannels()) {
        return SOUNDDEVICE_ERROR_EXCESSIVE_INPUT_CHANNEL;
    }
    m_audioInputs.append(in);
    return SOUNDDEVICE_ERROR_OK;
}

void SoundDevice::clearInputs() {
    m_audioInputs.clear();
}

bool SoundDevice::operator==(const SoundDevice &other) const {
    return this->getInternalName() == other.getInternalName();
}

bool SoundDevice::operator==(const QString &other) const {
    return getInternalName() == other;
}

void SoundDevice::composeOutputBuffer(float* outputBuffer,
                                      const unsigned long iFramesPerBuffer,
                                      const unsigned int iFrameSize) {
    // qDebug() << "SoundManager::composeOutputBuffer()"
    //          << device->getInternalName()
    //          << iFramesPerBuffer << iFrameSize;

    // Reset sample for each open channel
    memset(outputBuffer, 0, iFramesPerBuffer * iFrameSize * sizeof(*outputBuffer));

    // Interlace Audio data onto portaudio buffer.  We iterate through the
    // source list to find out what goes in the buffer data is interlaced in
    // the order of the list

    for (QList<AudioOutputBuffer>::iterator i = m_audioOutputs.begin(),
                 e = m_audioOutputs.end(); i != e; ++i) {
        AudioOutputBuffer& out = *i;

        const ChannelGroup outChans = out.getChannelGroup();
        const int iChannelCount = outChans.getChannelCount();
        const int iChannelBase = outChans.getChannelBase();

        const CSAMPLE* pAudioOutputBuffer = out.getBuffer();

        // All AudioOutputs are stereo as of Mixxx 1.12.0. If we have a mono
        // output then we need to downsample.
        if (iChannelCount == 1) {
            for (unsigned int i = 0; i < iFramesPerBuffer; ++i) {
                m_pDownmixBuffer[i] = (pAudioOutputBuffer[i*2] +
                                       pAudioOutputBuffer[i*2 + 1]) / 2.0f;
            }
            pAudioOutputBuffer = m_pDownmixBuffer;
        }

        for (unsigned int iFrameNo = 0; iFrameNo < iFramesPerBuffer; ++iFrameNo) {
            // iFrameBase is the "base sample" in a frame (ie. the first
            // sample in a frame)
            const unsigned int iFrameBase = iFrameNo * iFrameSize;
            const unsigned int iLocalFrameBase = iFrameNo * iChannelCount;

            // this will make sure a sample from each channel is copied
            for (int iChannel = 0; iChannel < iChannelCount; ++iChannel) {
                outputBuffer[iFrameBase + iChannelBase + iChannel] =
                        pAudioOutputBuffer[iLocalFrameBase + iChannel];

                // Input audio pass-through (useful for debugging)
                //if (in)
                //    output[iFrameBase + src.channelBase + iChannel] =
                //    in[iFrameBase + src.channelBase + iChannel];
            }
        }
    }
}
