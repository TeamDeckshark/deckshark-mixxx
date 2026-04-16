#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QDomNode>

#include "moc_waveformrenderbeat.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/unicolormaterial.h"
#include "rendergraph/vertexupdaters/vertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

using namespace rendergraph;

namespace allshader {

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    initForRectangles<UniColorMaterial>(0);
    setUsePreprocess(true);

    auto pSubNode = std::make_unique<GeometryNode>();
    m_pSubNode = pSubNode.get();
    m_pSubNode->initForRectangles<UniColorMaterial>(0);
    appendChildNode(std::move(pSubNode));
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& skinContext) {
    m_color = QColor(skinContext.selectString(node, QStringLiteral("BeatColor")));
    m_color = WSkinColor::getCorrectColor(m_color).toRgb();

    m_subColor = QColor(skinContext.selectString(
            node, QStringLiteral("BeatSubdivisionColor")));
    if (m_subColor.isValid()) {
        m_subColor = WSkinColor::getCorrectColor(m_subColor).toRgb();
    }

    m_beatThickness = skinContext.selectDouble(
            node, QStringLiteral("BeatThickness"), 1.0);
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderBeat::preprocess() {
    if (!preprocessInner()) {
        geometry().allocate(0);
        markDirtyGeometry();
        m_pSubNode->geometry().allocate(0);
        m_pSubNode->markDirtyGeometry();
    }
}

bool WaveformRenderBeat::preprocessInner() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return false;
    }

    const bool showSubdivisions = m_subColor.isValid();
#ifndef __SCENEGRAPH__
    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return false;
    }
    m_color.setAlphaF(alpha / 100.0f);
    if (showSubdivisions) {
        m_subColor.setAlphaF(alpha / 100.0f);
    }
#endif

    if (!m_color.alpha() && !(showSubdivisions && m_subColor.alpha())) {
        // Don't render the beatgrid lines if they are fully transparent
        return true;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return false;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    // Count the number of beats in the range to reserve space in the m_vertices vector.
    // Note that we could also use
    //   int numBearsInRange = trackBeats->numBeatsInRange(startPosition, endPosition);
    // for this, but there have been reports of that method failing with a DEBUG_ASSERT.
    int numBeatsInRange = 0;
    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        numBeatsInRange++;
    }

    const int onBeatVertices = numBeatsInRange * numVerticesPerLine;
    const int subVertices = showSubdivisions ? numBeatsInRange * 3 * numVerticesPerLine : 0;
    geometry().allocate(onBeatVertices);
    m_pSubNode->geometry().allocate(subVertices);

    VertexUpdater beatUpdater{geometry().vertexDataAs<Geometry::Point2D>()};
    VertexUpdater subUpdater{m_pSubNode->geometry().vertexDataAs<Geometry::Point2D>()};

    const float breadth = m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth;
    const float subBreadth = breadth * 0.5f;
    const float subStart = (breadth - subBreadth) * 0.5f;
    const float subEnd = subStart + subBreadth;
    const float halfThickness = static_cast<float>(m_beatThickness * 0.5);

    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        const auto beatPos = *it;
        const auto beatLength = it.beatLengthFrames();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        beatPos.toEngineSamplePos(), positionType);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

        const float x1 = static_cast<float>(xBeatPoint);
        // On-beat: m_beatThickness wide, full breadth, centered on the beat.
        beatUpdater.addRectangle(
                {x1 + 0.5f - halfThickness, 0.f},
                {x1 + 0.5f + halfThickness, breadth});

        if (!showSubdivisions) {
            continue;
        }

        // 3 subdivisions at +1/4, +1/2, +3/4 of this beat's length:
        // 1px wide, centered half-breadth.
        for (int i = 1; i <= 3; ++i) {
            const auto subPos = beatPos + beatLength * i / 4.0;
            double xSub = m_waveformRenderer->transformSamplePositionInRendererWorld(
                    subPos.toEngineSamplePos(), positionType);
            xSub = qRound(xSub * devicePixelRatio) / devicePixelRatio;
            const float sx1 = static_cast<float>(xSub);
            subUpdater.addRectangle({sx1, subStart}, {sx1 + 1.f, subEnd});
        }
    }
    markDirtyGeometry();
    m_pSubNode->markDirtyGeometry();

    DEBUG_ASSERT(onBeatVertices == beatUpdater.index());
    DEBUG_ASSERT(subVertices == subUpdater.index());

    material().setUniform(1, m_color);
    markDirtyMaterial();
    if (showSubdivisions) {
        m_pSubNode->material().setUniform(1, m_subColor);
        m_pSubNode->markDirtyMaterial();
    }

    return true;
}

} // namespace allshader
