/*
 *  SPDX-FileCopyrightText: 2020 Peter Schatz <voronwe13@gmail.com>
 *  SPDX-FileCopyrightText: 2021 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KoCompositeOpRegistry.h>
#include "KisColorSmudgeStrategyBase.h"

#include <filter/kis_filter_registry.h>
#include <filter/kis_filter.h>
#include <filter/kis_filter_configuration.h>
#include <KisGlobalResourcesInterface.h>

#include "kis_painter.h"
#include "kis_fixed_paint_device.h"
#include "kis_paint_device.h"
#include <kis_transaction.h>
#include "KisColorSmudgeSampleUtils.h"

/**********************************************************************************/
/*                 DabColoringStrategyMask                                        */
/**********************************************************************************/

bool KisColorSmudgeStrategyBase::DabColoringStrategyMask::supportsFusedDullingBlending() const
{
    return true;
}

void KisColorSmudgeStrategyBase::DabColoringStrategyMask::blendInFusedBackgroundAndColorRateWithDulling(
        KisFixedPaintDeviceSP dst, KisColorSmudgeSourceSP src, const QRect &dstRect,
        const KoColor &preparedDullingColor, const KoCompositeOp *smearOp, const quint8 smudgeRateOpacity,
        const KoColor &paintColor, const KoCompositeOp *colorRateOp, const quint8 colorRateOpacity) const
{
    KoColor dullingFillColor(preparedDullingColor);

    KIS_SAFE_ASSERT_RECOVER_RETURN(*paintColor.colorSpace() == *colorRateOp->colorSpace());
    colorRateOp->composite(dullingFillColor.data(), 1, paintColor.data(), 1, 0, 0, 1, 1, colorRateOpacity);

    if (smearOp->id() == COMPOSITE_COPY && smudgeRateOpacity == OPACITY_OPAQUE_U8) {
        dst->fill(dst->bounds(), dullingFillColor);
    } else {
        src->readBytes(dst->data(), dstRect);
        smearOp->composite(dst->data(), dstRect.width() * dst->pixelSize(),
                           dullingFillColor.data(), 0,
                           0, 0,
                           1, dstRect.width() * dstRect.height(),
                           smudgeRateOpacity);
    }
}

void KisColorSmudgeStrategyBase::DabColoringStrategyMask::blendInColorRate(const KoColor &paintColor,
                                                                           const KoCompositeOp *colorRateOp,
                                                                           quint8 colorRateOpacity,
                                                                           KisFixedPaintDeviceSP dstDevice,
                                                                           const QRect &dstRect) const
{
    KIS_SAFE_ASSERT_RECOVER_RETURN(*paintColor.colorSpace() == *colorRateOp->colorSpace());

    colorRateOp->composite(dstDevice->data(), dstRect.width() * dstDevice->pixelSize(),
                           paintColor.data(), 0,
                           0, 0,
                           dstRect.height(), dstRect.width(),
                           colorRateOpacity);
}

/**********************************************************************************/
/*                 DabColoringStrategyStamp                                       */
/**********************************************************************************/

void KisColorSmudgeStrategyBase::DabColoringStrategyStamp::setStampDab(KisFixedPaintDeviceSP device)
{
    m_origDab = device;
}

void KisColorSmudgeStrategyBase::DabColoringStrategyStamp::blendInColorRate(const KoColor &paintColor,
                                                                            const KoCompositeOp *colorRateOp,
                                                                            quint8 colorRateOpacity,
                                                                            KisFixedPaintDeviceSP dstDevice,
                                                                            const QRect &dstRect) const
{
    Q_UNUSED(paintColor);

    // TODO: check correctness for composition source device (transparency masks)
    KIS_ASSERT_RECOVER_RETURN(*dstDevice->colorSpace() == *m_origDab->colorSpace());

    colorRateOp->composite(dstDevice->data(), dstRect.width() * dstDevice->pixelSize(),
                           m_origDab->data(), dstRect.width() * m_origDab->pixelSize(),
                           0, 0,
                           dstRect.height(), dstRect.width(),
                           colorRateOpacity);
}

bool KisColorSmudgeStrategyBase::DabColoringStrategyStamp::supportsFusedDullingBlending() const
{
    return false;
}

void KisColorSmudgeStrategyBase::DabColoringStrategyStamp::blendInFusedBackgroundAndColorRateWithDulling(
        KisFixedPaintDeviceSP dst, KisColorSmudgeSourceSP src, const QRect &dstRect,
        const KoColor &preparedDullingColor, const KoCompositeOp *smearOp, const quint8 smudgeRateOpacity,
        const KoColor &paintColor, const KoCompositeOp *colorRateOp, const quint8 colorRateOpacity) const
{
    Q_UNUSED(dst);
    Q_UNUSED(src);
    Q_UNUSED(dstRect);
    Q_UNUSED(preparedDullingColor);
    Q_UNUSED(smearOp);
    Q_UNUSED(smudgeRateOpacity);
    Q_UNUSED(paintColor);
    Q_UNUSED(colorRateOp);
    Q_UNUSED(colorRateOpacity);
}

/**********************************************************************************/
/*                 KisColorSmudgeStrategyBase                                     */
/**********************************************************************************/

KisColorSmudgeStrategyBase::KisColorSmudgeStrategyBase(KisPainter *painter, KisSmudgeOption::Mode smudgeMode)
        : m_initializationPainter(painter)
        , m_smudgeMode(smudgeMode)
{
}

KisColorSmudgeStrategyBase::~KisColorSmudgeStrategyBase()
{
}

void KisColorSmudgeStrategyBase::initializePaintingImpl(const KoColorSpace *dstColorSpace, bool smearAlpha,
                                                        const QString &colorRateCompositeOpId)
{
    m_blendDevice = new KisFixedPaintDevice(dstColorSpace, m_memoryAllocator);
    m_smearOp = dstColorSpace->compositeOp(smearCompositeOp(smearAlpha));
    m_colorRateOp = dstColorSpace->compositeOp(colorRateCompositeOpId);
    m_preparedDullingColor.convertTo(dstColorSpace);

    if (m_smudgeMode == KisSmudgeOption::BLURRING_MODE) {
        m_filter = KisFilterRegistry::instance()->get("gaussian blur");
        if (m_filter) {
            m_filterDevice = m_initializationPainter->device()->createCompositionSourceDevice();
            m_filterConfiguration = new KisFilterConfiguration("gaussian blur", 1, KisGlobalResourcesInterface::instance());
            m_filterConfiguration->setProperty("horizRadius", 5);
            m_filterConfiguration->setProperty("vertRadius", 5);
            m_filterConfiguration->setProperty("lockAspect", true);
        } else {
            m_smudgeMode = KisSmudgeOption::DULLING_MODE;
        }
    }
}

const KoColorSpace *KisColorSmudgeStrategyBase::preciseColorSpace() const
{
    // verify that initialize() has already been called!
    KIS_ASSERT_RECOVER_RETURN_VALUE(m_smearOp, KoColorSpaceRegistry::instance()->rgb8());

    return m_smearOp->colorSpace();
}

QString KisColorSmudgeStrategyBase::smearCompositeOp(bool smearAlpha) const
{
    return smearAlpha ? COMPOSITE_COPY : COMPOSITE_OVER;
}

QString KisColorSmudgeStrategyBase::finalCompositeOp(bool smearAlpha) const
{
    Q_UNUSED(smearAlpha);
    return COMPOSITE_COPY;
}

quint8 KisColorSmudgeStrategyBase::finalPainterOpacity(qreal opacity, qreal smudgeRateValue)
{
    Q_UNUSED(opacity);
    Q_UNUSED(smudgeRateValue);

    return OPACITY_OPAQUE_U8;
}

quint8 KisColorSmudgeStrategyBase::colorRateOpacity(qreal opacity, qreal smudgeRateValue, qreal colorRateValue,
                                                    qreal maxPossibleSmudgeRateValue)
{
    Q_UNUSED(smudgeRateValue);
    Q_UNUSED(maxPossibleSmudgeRateValue);
    return qRound(colorRateValue * colorRateValue * opacity * 255.0);
}

quint8 KisColorSmudgeStrategyBase::dullingRateOpacity(qreal opacity, qreal smudgeRateValue)
{
    return qRound(0.8 * smudgeRateValue * opacity * 255.0);
}

quint8 KisColorSmudgeStrategyBase::smearRateOpacity(qreal opacity, qreal smudgeRateValue)
{
    return qRound(smudgeRateValue * opacity * 255.0);
}

void KisColorSmudgeStrategyBase::sampleDullingColor(const QRect &srcRect, qreal sampleRadiusValue,
                                                    KisColorSmudgeSourceSP sourceDevice,
                                                    KisFixedPaintDeviceSP tempFixedDevice,
                                                    KisFixedPaintDeviceSP maskDab, KoColor *resultColor)
{
    using namespace KisColorSmudgeSampleUtils;
    sampleColor<WeightedSampleWrapper>(srcRect, sampleRadiusValue,
                                       sourceDevice, tempFixedDevice,
                                       maskDab, resultColor);
}

void
KisColorSmudgeStrategyBase::blendBrush(const QVector<KisPainter *> dstPainters, KisColorSmudgeSourceSP srcSampleDevice,
                                       KisFixedPaintDeviceSP maskDab, bool preserveMaskDab, const QRect &srcRect,
                                       const QRect &dstRect, const KoColor &currentPaintColor, qreal opacity,
                                       qreal smudgeRateValue, qreal maxPossibleSmudgeRateValue, qreal colorRateValue,
                                       qreal smudgeRadiusValue)
{
    const quint8 colorRateOpacity = this->colorRateOpacity(opacity, smudgeRateValue, colorRateValue, maxPossibleSmudgeRateValue);

    if (m_smudgeMode == KisSmudgeOption::DULLING_MODE ||
        (m_smudgeMode == KisSmudgeOption::BLURRING_MODE && smudgeRadiusValue > 1.0)) {
        this->sampleDullingColor(srcRect,
                                 m_smudgeMode == KisSmudgeOption::BLURRING_MODE ? 1.0 : smudgeRadiusValue,
                                 srcSampleDevice, m_blendDevice,
                                 maskDab, &m_preparedDullingColor);

        KIS_SAFE_ASSERT_RECOVER(*m_preparedDullingColor.colorSpace() == *m_colorRateOp->colorSpace()) {
            m_preparedDullingColor.convertTo(m_colorRateOp->colorSpace());
        }
    }

    m_blendDevice->setRect(dstRect);
    m_blendDevice->lazyGrowBufferWithoutInitialization();

    DabColoringStrategy &coloringStrategy = this->coloringStrategy();

    const quint8 dullingRateOpacity = this->dullingRateOpacity(opacity, smudgeRateValue);

    if (colorRateOpacity > 0 &&
        m_smudgeMode == KisSmudgeOption::DULLING_MODE &&
        coloringStrategy.supportsFusedDullingBlending() &&
        ((m_smearOp->id() == COMPOSITE_OVER &&
          m_colorRateOp->id() == COMPOSITE_OVER) ||
         (m_smearOp->id() == COMPOSITE_COPY &&
          dullingRateOpacity == OPACITY_OPAQUE_U8))) {

        coloringStrategy.blendInFusedBackgroundAndColorRateWithDulling(m_blendDevice,
                                                                       srcSampleDevice,
                                                                       dstRect,
                                                                       m_preparedDullingColor,
                                                                       m_smearOp,
                                                                       dullingRateOpacity,
                                                                       currentPaintColor.convertedTo(
                                                                               m_preparedDullingColor.colorSpace()),
                                                                       m_colorRateOp,
                                                                       colorRateOpacity);

    } else {
        if (m_smudgeMode == KisSmudgeOption::SMEARING_MODE) {
            const quint8 smudgeRateOpacity = this->smearRateOpacity(opacity, smudgeRateValue);
            blendInBackgroundWithSmearing(m_blendDevice, srcSampleDevice,
                                          srcRect, dstRect, smudgeRateOpacity);
        } else if (m_smudgeMode == KisSmudgeOption::DULLING_MODE) {
            blendInBackgroundWithDulling(m_blendDevice, srcSampleDevice,
                                         dstRect,
                                         m_preparedDullingColor, dullingRateOpacity);
        } else {
            const quint8 smudgeRateOpacity = this->smearRateOpacity(opacity, smudgeRateValue);
            blendInBackgroundWithBlurring(m_blendDevice, srcSampleDevice,
                                          dstRect,
                                          m_preparedDullingColor,
                                          smudgeRateOpacity, smudgeRadiusValue);
        }

        if (colorRateOpacity > 0) {
            coloringStrategy.blendInColorRate(
                    currentPaintColor.convertedTo(m_preparedDullingColor.colorSpace()),
                    m_colorRateOp,
                    colorRateOpacity,
                    m_blendDevice, dstRect);
        }
    }

    const bool preserveDab = preserveMaskDab && dstPainters.size() > 1;

    Q_FOREACH (KisPainter *dstPainter, dstPainters) {
        dstPainter->setOpacity(finalPainterOpacity(opacity, smudgeRateValue));

        dstPainter->bltFixedWithFixedSelection(dstRect.x(), dstRect.y(),
                                               m_blendDevice, maskDab,
                                               maskDab->bounds().x(), maskDab->bounds().y(),
                                               m_blendDevice->bounds().x(), m_blendDevice->bounds().y(),
                                               dstRect.width(), dstRect.height());
        dstPainter->renderMirrorMaskSafe(dstRect, m_blendDevice, maskDab, preserveDab);
    }

}

void KisColorSmudgeStrategyBase::blendInBackgroundWithSmearing(KisFixedPaintDeviceSP dst, KisColorSmudgeSourceSP src,
                                                               const QRect &srcRect, const QRect &dstRect,
                                                               const quint8 smudgeRateOpacity)
{
    if (m_smearOp->id() == COMPOSITE_COPY && smudgeRateOpacity == OPACITY_OPAQUE_U8) {
        src->readBytes(dst->data(), srcRect);
    } else {
        src->readBytes(dst->data(), dstRect);

        KisFixedPaintDevice tempDevice(src->colorSpace(), m_memoryAllocator);
        tempDevice.setRect(srcRect);
        tempDevice.lazyGrowBufferWithoutInitialization();

        src->readBytes(tempDevice.data(), srcRect);
        m_smearOp->composite(dst->data(), dstRect.width() * dst->pixelSize(),
                             tempDevice.data(), dstRect.width() * tempDevice.pixelSize(), // stride should be random non-zero
                             0, 0,
                             1, dstRect.width() * dstRect.height(),
                             smudgeRateOpacity);
    }
}

void KisColorSmudgeStrategyBase::blendInBackgroundWithDulling(KisFixedPaintDeviceSP dst, KisColorSmudgeSourceSP src,
                                                              const QRect &dstRect, const KoColor &preparedDullingColor,
                                                              const quint8 smudgeRateOpacity)
{
    Q_UNUSED(preparedDullingColor);

    if (m_smearOp->id() == COMPOSITE_COPY && smudgeRateOpacity == OPACITY_OPAQUE_U8) {
        dst->fill(dst->bounds(), m_preparedDullingColor);
    } else {
        src->readBytes(dst->data(), dstRect);
        m_smearOp->composite(dst->data(), dstRect.width() * dst->pixelSize(),
                             m_preparedDullingColor.data(), 0,
                             0, 0,
                             1, dstRect.width() * dstRect.height(),
                             smudgeRateOpacity);
    }
}

void KisColorSmudgeStrategyBase::blendInBackgroundWithBlurring(KisFixedPaintDeviceSP dst, KisColorSmudgeSourceSP src,
                                                               const QRect &dstRect,
                                                               const KoColor &preparedDullingColor,
                                                               const quint8 smudgeRateOpacity, const qreal smudgeRadiusValue)
{
    // TODO
    // Radius is clipped to 1.0 (100%)
    // Radius above 1.0 will blend in the dulling color on top of blurring
    m_filterConfiguration->setProperty("horizRadius", 30.0); // TODO
    m_filterConfiguration->setProperty("vertRadius", 30.0); // TODO
    QRect neededRect = m_filter->neededRect(dstRect, m_filterConfiguration, m_initializationPainter->device()->defaultBounds()->currentLevelOfDetail());

    // Copy the original data to the destination
    // src->readBytes(dst->data(), dstRect);

    // Copy the original data into the blurring device
    // KisPainter p(m_filterDevice);
    // p.bitBltOldData(neededRect.topLeft(), src, neededRect);

	// 
    // KisFixedPaintDevice tempDevice(src->colorSpace(), m_memoryAllocator);
    // tempDevice.setRect(neededRect);
    // tempDevice.lazyGrowBufferWithoutInitialization();
	// 
    // // Copy the original data into the blurring device
    // src->readBytes(tempDevice.data(), neededRect);
	// 
    // // Apply the blur
    // KisTransaction transaction(tempDevice);
    // m_filter->process(tempDevice, dstRect, m_filterConfiguration, 0);
    // transaction.end();

    // Blend the blur with the destination
    // m_smearOp->composite(dst->data(), dstRect.width() * dst->pixelSize(),
    //                      tempDevice.data(), dstRect.width() * tempDevice.pixelSize(), // stride should be random non-zero
    //                      0, 0,
    //                      1, dstRect.width() * dstRect.height(),
    //                      smudgeRateOpacity);
}
