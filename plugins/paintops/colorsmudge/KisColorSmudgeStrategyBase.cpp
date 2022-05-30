/*
 *  SPDX-FileCopyrightText: 2020 Peter Schatz <voronwe13@gmail.com>
 *  SPDX-FileCopyrightText: 2021 Dmitry Kazakov <dimula73@gmail.com>
 *  SPDX-FileCopyrightText: 2022 Jan Boon <jan.boon@kaetemi.be>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <KoCompositeOpRegistry.h>
#include "KisColorSmudgeStrategyBase.h"

#include <kis_convolution_kernel.h>
#include <kis_convolution_painter.h>
#include <kis_gaussian_kernel.h>
#include <kis_lod_transform.h>
#include <kis_transaction.h>
#include <kis_random_sub_accessor.h>

#include "kis_painter.h"
#include "kis_fixed_paint_device.h"
#include "kis_paint_device.h"
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

KisColorSmudgeStrategyBase::KisColorSmudgeStrategyBase(KisPainter *painter, KisSmudgeOption::Mode smudgeMode, bool smudgeScaling)
        : m_initializationPainter(painter)
        , m_smudgeMode(smudgeMode)
        , m_smudgeScaling(smudgeScaling)
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

    if (m_smudgeMode == KisSmudgeOption::BLURRING_MODE || m_smudgeScaling) {
        m_filterDevice = new KisPaintDevice(dstColorSpace);
        m_filterDevice->setDefaultBounds(m_initializationPainter->device()->defaultBounds());
    }
}

QRect KisColorSmudgeStrategyBase::neededRect(const QRect &srcRect, qreal radiusFactor, qreal scalingFactor)
{
    if (m_smudgeMode == KisSmudgeOption::BLURRING_MODE) {
        int lod = m_initializationPainter->device()->defaultBounds()->currentLevelOfDetail();
        const qreal horizRadius = ((qreal)srcRect.width()) * radiusFactor / 2.0; 
        const qreal vertRadius = ((qreal)srcRect.height()) * radiusFactor / 2.0; 
        KisLodTransformScalar t(lod);
        const int halfWidth = KisGaussianKernel::kernelSizeFromRadius(t.scale(horizRadius)) / 2;
        const int halfHeight = KisGaussianKernel::kernelSizeFromRadius(t.scale(vertRadius)) / 2;
        if (m_smudgeScaling) {
            return neededScaleUpRect(srcRect, scalingFactor).adjusted(
                -halfWidth * 2, -halfHeight * 2, halfWidth * 2, halfHeight * 2);
        }
        return srcRect.adjusted(-halfWidth * 2, -halfHeight * 2, halfWidth * 2, halfHeight * 2);
    }
    return srcRect;
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
KisColorSmudgeStrategyBase::blendBrush(const QVector<KisPainter *> &dstPainters, KisColorSmudgeSourceSP srcSampleDevice,
                                       KisFixedPaintDeviceSP maskDab, bool preserveMaskDab, const QRect &neededRect,
                                       const QRect &srcRect, const QRect &dstRect, const KoColor &currentPaintColor, qreal opacity,
                                       qreal smudgeRateValue, qreal maxPossibleSmudgeRateValue, qreal smudgeScalingValue,
                                       qreal colorRateValue, qreal smudgeRadiusValue)
{
    const quint8 colorRateOpacity = this->colorRateOpacity(opacity, smudgeRateValue, colorRateValue, maxPossibleSmudgeRateValue);

    if (m_smudgeMode == KisSmudgeOption::DULLING_MODE) {
        this->sampleDullingColor(srcRect,
                                 smudgeRadiusValue,
                                 srcSampleDevice, m_blendDevice,
                                 maskDab, &m_preparedDullingColor);

        KIS_SAFE_ASSERT_RECOVER(*m_preparedDullingColor.colorSpace() == *m_colorRateOp->colorSpace()) {
            m_preparedDullingColor.convertTo(m_colorRateOp->colorSpace());
        }
    }

    m_blendDevice->setRect(dstRect);
    m_blendDevice->lazyGrowBufferWithoutInitialization();

    const DabColoringStrategy &coloringStrategy = this->coloringStrategy();

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
        if (m_smudgeMode == KisSmudgeOption::DULLING_MODE) {
            blendInBackgroundWithDulling(m_blendDevice, srcSampleDevice,
                                         dstRect,
                                         m_preparedDullingColor, dullingRateOpacity);
        } else if (m_smudgeMode == KisSmudgeOption::BLURRING_MODE) {
            const quint8 smudgeRateOpacity = this->smearRateOpacity(opacity, smudgeRateValue);
            blendInBackgroundWithBlurring(m_blendDevice, srcSampleDevice,
                                          neededRect, srcRect, dstRect,
                                          smudgeRateOpacity, smudgeRadiusValue, smudgeScalingValue);
        } else {
            const quint8 smudgeRateOpacity = this->smearRateOpacity(opacity, smudgeRateValue);
            blendInBackgroundWithSmearing(m_blendDevice, srcSampleDevice,
                                          srcRect, dstRect, smudgeRateOpacity, smudgeScalingValue);
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
                                                               const quint8 smudgeRateOpacity, const qreal smudgeScalingValue)
{
    const bool opaqueCopy = m_smearOp->id() == COMPOSITE_COPY && smudgeRateOpacity == OPACITY_OPAQUE_U8;
    const bool useScaling = m_smudgeScaling && smudgeScalingValue != 1.0;
    
    if (!opaqueCopy) {
        // Copy the original data to the destination
        src->readBytes(dst->data(), dstRect);
    }

    KisFixedPaintDevice tempDevice(src->colorSpace(), m_memoryAllocator);
    if (!opaqueCopy) {
        tempDevice.setRect(dstRect);
        tempDevice.lazyGrowBufferWithoutInitialization();
    }
    if (useScaling)
    {
        // Copy the original data into the filtering device
        KisPainter p(m_filterDevice);
        p.setCompositeOpId(COMPOSITE_COPY);
        src->bitBlt(&p, srcRect.topLeft(), srcRect);

        // Scale 1x - 2x
        scaleUp(opaqueCopy ? *dst : tempDevice, dstRect,
                m_filterDevice, srcRect, smudgeScalingValue);
        m_filterDevice->clear();
    }

    if (opaqueCopy) {
        if (!useScaling)
        {
            // Write src directly to dst
            src->readBytes(dst->data(), srcRect);
        }
    } else {
        if (!useScaling)
        {
            // Write src to temp device
            src->readBytes(tempDevice.data(), srcRect);
        }

        // Blend the smear with the destination
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
                                                               const QRect &neededRect, const QRect &srcRect, const QRect &dstRect,
                                                               const quint8 smudgeRateOpacity, const qreal smudgeRadiusValue,
                                                               const qreal smudgeScalingValue)
{
    const bool opaqueCopy = m_smearOp->id() == COMPOSITE_COPY && smudgeRateOpacity == OPACITY_OPAQUE_U8;
    const bool useScaling = m_smudgeScaling && smudgeScalingValue != 1.0;
    
    if (!opaqueCopy) {
        // Copy the original data to the destination
        src->readBytes(dst->data(), dstRect);
    }

    // Copy the original data into the blurring device
    KisPainter p(m_filterDevice);
    p.setCompositeOpId(COMPOSITE_COPY);
    src->bitBlt(&p, neededRect.topLeft(), neededRect);

    // Blur
    KisTransaction transaction(m_filterDevice);
    KisLodTransformScalar t(m_filterDevice);
    const qreal horizRadius = t.scale(((qreal)srcRect.width()) * smudgeRadiusValue / 2.0);
    const qreal vertRadius = t.scale(((qreal)srcRect.height()) * smudgeRadiusValue / 2.0);
    QBitArray channelFlags = QBitArray(m_filterDevice->colorSpace()->channelCount(), true);
    KisGaussianKernel::applyGaussian(m_filterDevice, useScaling ? neededScaleUpRect(srcRect, smudgeScalingValue) : srcRect,
                                     horizRadius, vertRadius,
                                     channelFlags, nullptr);
    transaction.end();

    KisFixedPaintDevice tempDevice(src->colorSpace(), m_memoryAllocator);
    if (!opaqueCopy) {
        tempDevice.setRect(dstRect);
        tempDevice.lazyGrowBufferWithoutInitialization();
    }
    if (useScaling)
    {
        // Scale 1x - 2x
        scaleUp(opaqueCopy ? *dst : tempDevice, dstRect,
                m_filterDevice, srcRect, smudgeScalingValue);
        m_filterDevice->clear();
    }

    if (opaqueCopy) {
        if (!useScaling)
        {
            // Write blur directly to dst
            m_filterDevice->readBytes(dst->data(), srcRect);
            m_filterDevice->clear();
        }
    } else {
        if (!useScaling)
        {
            // Write blur to temp device
            m_filterDevice->readBytes(tempDevice.data(), srcRect);
            m_filterDevice->clear();
        }

        // Blend the blur with the destination
        m_smearOp->composite(dst->data(), dstRect.width() * dst->pixelSize(),
                             tempDevice.data(), dstRect.width() * tempDevice.pixelSize(), // stride should be random non-zero
                             0, 0,
                             1, dstRect.width() * dstRect.height(),
                             smudgeRateOpacity);
    }
}

QRect KisColorSmudgeStrategyBase::neededScaleUpRect(const QRect &rect, const qreal factor)
{
    const qreal horizMid = (qreal)rect.width() * 0.5;
    const qreal vertMid = (qreal)rect.height() * 0.5;
    const qreal scaleFactor = 1.0 / factor;
    qreal xOffsetLeft = -horizMid;
    qreal xOffHalfLeft = xOffsetLeft * scaleFactor;
    int xSrcLeft = rect.x() + qFloor(xOffHalfLeft + horizMid);
    qreal xOffsetRight = (qreal)rect.width() - 1 - horizMid;
    qreal xOffHalfRight = xOffsetRight * scaleFactor;
    int xSrcRight = rect.x() + qCeil(xOffHalfRight + horizMid);
    qreal yOffsetTop = -vertMid;
    qreal yOffHalfTop = yOffsetTop * scaleFactor;
    int ySrcTop = rect.y() + qFloor(yOffHalfTop + vertMid);
    qreal yOffsetBottom = (qreal)rect.height() - 1 - vertMid;
    qreal yOffHalfBottom = yOffsetBottom * scaleFactor;
    int ySrcBottom = rect.y() + qCeil(yOffHalfBottom + vertMid);
    QRect res = QRect(xSrcLeft, ySrcTop, xSrcRight - xSrcLeft + 1, ySrcBottom - ySrcTop + 1);
    return res;
}

void KisColorSmudgeStrategyBase::scaleUp(KisFixedPaintDevice &dst, const QRect &dstRect,
                                         KisPaintDeviceSP src, const QRect &srcRect, const qreal factor)
{
    KisRandomSubAccessorSP accessor = src->createRandomSubAccessor();
    qreal horizMid = (qreal)dstRect.width() * 0.5;
    qreal vertMid = (qreal)dstRect.height() * 0.5;
    qreal scaleFactor = 1.0 / factor;
    for (int y = 0; y < dstRect.height(); ++y) {
        qreal yOffset = (qreal)y - vertMid;
        qreal yOffHalf = yOffset * scaleFactor;
        qreal ySrc = (qreal)srcRect.y() + yOffHalf + vertMid;
        for (int x = 0; x < dstRect.width(); ++x) {
            qreal xOffset = (qreal)x - horizMid;
            qreal xOffHalf = xOffset * scaleFactor;
            qreal xSrc = (qreal)srcRect.x() + xOffHalf + horizMid;
            accessor->moveTo(xSrc, ySrc);
            accessor->sampledRawData(dst.data() + (dstRect.width() * y + x) * dst.pixelSize());
        }
    }
}
