/*
 * AudioSoundIo.h - device-class that performs PCM-output via libsoundio
 *
 * Copyright (c) 2015 Andrew Kelley <superjoe30@gmail.com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_AUDIO_SOUNDIO_H
#define LMMS_AUDIO_SOUNDIO_H


#include "lmmsconfig.h"

#ifdef LMMS_HAVE_SOUNDIO

#include <soundio/soundio.h>

#include <QString>

#include "AudioDevice.h"

namespace lmms
{



class AudioSoundIo : public AudioDevice
{
public:
	AudioSoundIo( bool & _success_ful, AudioEngine* audioEngine );
	virtual ~AudioSoundIo();

	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "AudioDeviceSetupWidget", "soundio" );
	}

private:
	void startProcessingImpl() override;
	void stopProcessingImpl() override;

	SoundIo *m_soundio;
	SoundIoOutStream *m_outstream;

	bool m_stopped;
	bool m_outstreamStarted;

	int m_disconnectErr;
	void onBackendDisconnect(int err);

	void writeCallback(int frame_count_min, int frame_count_max);
	void errorCallback(int err);
	void underflowCallback();

	static void staticWriteCallback(SoundIoOutStream *outstream, int frame_count_min, int frame_count_max) {
		return ((AudioSoundIo *)outstream->userdata)->writeCallback(frame_count_min, frame_count_max);
	}
	static void staticErrorCallback(SoundIoOutStream *outstream, int err) {
		return ((AudioSoundIo *)outstream->userdata)->errorCallback(err);
	}
	static void staticUnderflowCallback(SoundIoOutStream *outstream) {
		return ((AudioSoundIo *)outstream->userdata)->underflowCallback();
	}
	static void staticOnBackendDisconnect(SoundIo *soundio, int err) {
		return ((AudioSoundIo *)soundio->userdata)->onBackendDisconnect(err);
	}

};


} // namespace lmms

#endif // LMMS_HAVE_SOUNDIO

#endif // LMMS_AUDIO_SOUNDIO_H
