#include <QMutexLocker>

#include "AutomationClip.h"
#include "AutomationClipView.h"
#include "AutomationTrack.h"
#include "AutomationTrackView.h"
#include "Effect.h"
#include "EffectView.h"
#include "GuiMode.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "MidiClip.h"
#include "MidiClipView.h"
#include "PatternClip.h"
#include "PatternClipView.h"
#include "PatternTrack.h"
#include "PatternTrackView.h"
#include "Plugin.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "SampleTrack.h"
#include "SampleTrackView.h"
#include "TrackContainerView.h"
#include "TrackView.h"


namespace lmms
{


QWidget* AutomationTrack::createView(QWidget* tcv)
{
	return new gui::AutomationTrackView(this, static_cast<gui::TrackContainerView*>(tcv));
}

QWidget* AutomationClip::createView(QWidget* _tv)
{
	QMutexLocker m(&m_clipMutex);
	return new gui::AutomationClipView(this, static_cast<gui::TrackView*>(_tv));
}

QWidget* Effect::instantiateView(QWidget* _parent)
{
	return new gui::EffectView(this, _parent);
}

QWidget* InstrumentTrack::createView(QWidget* tcv)
{
	return new gui::InstrumentTrackView(this, static_cast<gui::TrackContainerView*>(tcv));
}

QWidget* MidiClip::createView(QWidget* _tv)
{
	return new gui::MidiClipView(this, static_cast<gui::TrackView*>(_tv));
}

QWidget* PatternClip::createView(QWidget* tv)
{
	return new gui::PatternClipView(this, static_cast<gui::TrackView*>(tv));
}

QWidget* PatternTrack::createView(QWidget* tcv)
{
	return new gui::PatternTrackView(this, static_cast<gui::TrackContainerView*>(tcv));
}

QWidget* Plugin::createView(QWidget* parent)
{
	if (!isGuiMode()) { return nullptr; }
	return instantiateView(parent);
}

QWidget* SampleClip::createView(QWidget* _tv)
{
	return new gui::SampleClipView(this, static_cast<gui::TrackView*>(_tv));
}

QWidget* SampleTrack::createView(QWidget* tcv)
{
	return new gui::SampleTrackView(this, static_cast<gui::TrackContainerView*>(tcv));
}


} // namespace lmms
