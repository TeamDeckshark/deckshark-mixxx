#include "waveform/renderers/waveformrenderbeat.h"

#include <QPainter>

#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

class QPaintEvent;

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidgetRenderer)
        : WaveformRendererAbstract(waveformWidgetRenderer) {
    m_beats.resize(128);
    m_subdivisions.resize(128 * 3);
}

WaveformRenderBeat::~WaveformRenderBeat() {
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_beatColor = QColor(context.selectString(node, "BeatColor"));
    m_beatColor = WSkinColor::getCorrectColor(m_beatColor).toRgb();

    m_subBeatColor = QColor(context.selectString(node, "BeatSubdivisionColor"));
    if (m_subBeatColor.isValid()) {
        m_subBeatColor = WSkinColor::getCorrectColor(m_subBeatColor).toRgb();
    }

    m_beatThickness = context.selectDouble(node, "BeatThickness", 1.0);
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* /*event*/) {
    TrackPointer pTrackInfo = m_waveformRenderer->getTrackInfo();

    if (!pTrackInfo) {
        return;
    }

    mixxx::BeatsPointer trackBeats = pTrackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }
    const bool showSubdivisions = m_subBeatColor.isValid();
#ifdef MIXXX_USE_QOPENGL
    // Using alpha transparency with drawLines causes a graphical issue when
    // drawing with QPainter on the QOpenGLWindow: instead of individual lines
    // a large rectangle encompassing all beatlines is drawn.
    m_beatColor.setAlphaF(1.f);
    if (showSubdivisions) {
        m_subBeatColor.setAlphaF(1.f);
    }
#else
    m_beatColor.setAlphaF(alpha/100.0);
    if (showSubdivisions) {
        m_subBeatColor.setAlphaF(alpha/100.0);
    }
#endif

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition();
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition();

    // qDebug() << "trackSamples" << trackSamples
    //          << "firstDisplayedPosition" << firstDisplayedPosition
    //          << "lastDisplayedPosition" << lastDisplayedPosition;

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);
    auto it = trackBeats->iteratorFrom(startPosition);

    // if no beat do not waste time saving/restoring painter
    if (it == trackBeats->cend() || *it > endPosition) {
        return;
    }

    PainterScope PainterScope(painter);

    painter->setRenderHint(QPainter::Antialiasing);

    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();
    const float rendererWidth = m_waveformRenderer->getWidth();
    const float rendererHeight = m_waveformRenderer->getHeight();
    const float rendererBreadth =
            (orientation == Qt::Horizontal) ? rendererHeight : rendererWidth;
    const float subBreadth = rendererBreadth * 0.5f;
    const float subStart = (rendererBreadth - subBreadth) * 0.5f;
    const float subEnd = subStart + subBreadth;

    int beatCount = 0;
    int subCount = 0;

    for (; it != trackBeats->cend() && *it <= endPosition; ++it) {
        const auto beatPos = *it;
        const auto beatLength = it.beatLengthFrames();
        double xBeatPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                beatPos.toEngineSamplePos());

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        // If we don't have enough space, double the size.
        if (beatCount >= m_beats.size()) {
            m_beats.resize(m_beats.size() * 2);
        }

        if (orientation == Qt::Horizontal) {
            m_beats[beatCount++].setLine(xBeatPoint, 0.0f, xBeatPoint, rendererHeight);
        } else {
            m_beats[beatCount++].setLine(0.0f, xBeatPoint, rendererWidth, xBeatPoint);
        }

        if (!showSubdivisions) {
            continue;
        }

        if (subCount + 3 > m_subdivisions.size()) {
            m_subdivisions.resize(m_subdivisions.size() * 2);
        }

        for (int i = 1; i <= 3; ++i) {
            const auto subPos = beatPos + beatLength * i / 4.0;
            double xSubPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                    subPos.toEngineSamplePos());
            xSubPoint = qRound(xSubPoint * devicePixelRatio) / devicePixelRatio;

            if (orientation == Qt::Horizontal) {
                m_subdivisions[subCount++].setLine(xSubPoint, subStart, xSubPoint, subEnd);
            } else {
                m_subdivisions[subCount++].setLine(subStart, xSubPoint, subEnd, xSubPoint);
            }
        }
    }

    // Subdivisions first (thinner, shorter) so on-beat lines draw on top.
    if (showSubdivisions) {
        QPen subPen(m_subBeatColor);
        subPen.setWidthF(std::max(1.0, scaleFactor()));
        painter->setPen(subPen);
        painter->drawLines(m_subdivisions.constData(), subCount);
    }

    QPen beatPen(m_beatColor);
    beatPen.setWidthF(m_beatThickness * std::max(1.0, scaleFactor()));
    painter->setPen(beatPen);
    painter->drawLines(m_beats.constData(), beatCount);
}
