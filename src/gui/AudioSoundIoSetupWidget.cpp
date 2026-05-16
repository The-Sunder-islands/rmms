/*
 * AudioSoundIoSetupWidget.cpp - setup widget for SoundIO audio output
 *
 * Copyright (c) 2015-2024 Andrew Kelley <superjoe30@gmail.com>
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

#include "AudioSoundIoSetupWidget.h"
#include "AudioSoundIo.h"

namespace lmms::gui
{

AudioSoundIoSetupWidget::AudioSoundIoSetupWidget(QWidget* parent)
	: AudioDeviceSetupWidget(AudioSoundIo::name(), parent)
{
}

void AudioSoundIoSetupWidget::saveSettings()
{
}

} // namespace lmms::gui
